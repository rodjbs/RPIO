/*
 *
 * License
 *
 *     This program is free software: you can redistribute it and/or modify
 *     it under the terms of the GNU Lesser General Public License as published
 *     by the Free Software Foundation, either version 3 of the License, or
 *     (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU Lesser General Public License for more details at
 *     <http://www.gnu.org/licenses/lgpl-3.0-standalone.html>
 *
 * node_gpio.cc is based on RPIO by Chris Hager, which is based on RPi.GPIO
 * by Ben Croston, and provides a node.js interface to interact with the
 * gpio-related C methods.
 *
 */

#include <node.h>

#include "node_constants.hh"
#include "node_common.hh"

extern "C" {
#include "c_gpio.h"
}

using v8::FunctionCallbackInfo;
using v8::Isolate;
using v8::Object;
using v8::Local;
using v8::String;
using v8::Number;
using v8::Value;
using v8::Exception;
using v8::Object;
using v8::Context;


class Error {};
void err() { throw Error(); }

// Read /proc/cpuinfo once and keep the info at hand for further requests
static void
cache_rpi_revision(void)
{
    revision_int = get_cpuinfo_revision(revision_hex);
}

// bcm_to_board() returns the pin for the supplied bcm_gpio_id or -1
// if not a valid gpio-id. P5 pins are returned with | HEADER_P5, so
// you can know the header with (retval >> 8) (either 0 or 5) and the
// exact pin number with (retval & 255).
static int
bcm_to_board(int bcm_gpio_id)
{
    return *(*gpio_to_pin+bcm_gpio_id);
}

// channel_to_bcm() returns the bcm gpio id for the supplied channel
// depending on current setmode. Only P1 header channels are supported.
// To use P5 you need to use BCM gpio ids (`setmode(BCM)`).
static int
board_to_bcm(int board_pin_id)
{
    return *(*pin_to_gpio+board_pin_id);
}

// module_setup is run on import of the GPIO module and calls the setup() method in c_gpio.c
static int
module_setup(Isolate* isolate)
{
    int result;
    // printf("Setup module (mmap)\n");

    // Set all gpios to input in internal direction (not the system)
    int i=0;
    for (i=0; i<54; i++)
        gpio_direction[i] = -1;

    result = setup();
    if (result == SETUP_DEVMEM_FAIL) {
        isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "No access to /dev/mem. Try running as root!")));
        return SETUP_DEVMEM_FAIL;
    } else if (result == SETUP_MALLOC_FAIL) {
        isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "No memory")));
        return SETUP_MALLOC_FAIL;
    } else if (result == SETUP_MMAP_FAIL) {
        isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "Mmap failed on module import")));
        return SETUP_MALLOC_FAIL;
    } else {
        // result == SETUP_OK
        return SETUP_OK;
    }
}

// Sets everything back to input
// TODO find out if this actually does what it's supposed to
// I don't actually know anything about v8 nor garbage collection
void
cleanup(Isolate *isolate, v8::GCType type, v8::GCCallbackFlags flags)
{
    int i;
    for (i=0; i<54; i++) {
        if (gpio_direction[i] != -1) {
            // printf("GPIO %d --> INPUT\n", i);
            setup_gpio(i, INPUT, PUD_OFF);
            gpio_direction[i] = -1;
        }
    }
}

// channel_to_gpio tries to convert the supplied channel-id to
// a BCM GPIO ID based on current setmode. On error, throws js exception
// and returns a value < 0.
int
channel_to_gpio(Isolate* isolate, int channel)
{
    int gpio;

     if (gpio_mode != BOARD && gpio_mode != BCM) {
        isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "Please set pin numbering mode using RPIO.setmode(RPIO.BOARD) or RPIO.setmode(RPIO.BCM)")));
        return -1;
    }

   if ( (gpio_mode == BCM && (channel < 0 || channel > 31)) ||
        (gpio_mode == BOARD && (channel < 1 || channel > 41)) ) {
        isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "The channel sent is invalid on a Raspberry Pi (outside of range)")));
        return -2;
    }

    if (gpio_mode == BOARD) {
        if ((gpio = board_to_bcm(channel)) == -1) {
            isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "The channel sent is invalid on a Raspberry Pi (not a valid pin)")));
            return -3;
        }
    } else {
        // gpio_mode == BCM
        gpio = channel;
        if (bcm_to_board(gpio) == -1) {
            isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "The channel sent is invalid on a Raspberry Pi (not a valid gpio)")));
            return -3;
        }
    }

    //printf("channel2bcm: %d -> %d", channel, gpio);
    return gpio;
}

static int
verify_input(Isolate* isolate, int channel, int *gpio)
{
    if ((*gpio = channel_to_gpio(isolate, channel)) == -1)
        return 0;

    if ((gpio_direction[*gpio] != INPUT) && (gpio_direction[*gpio] != OUTPUT)) {
        isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "GPIO channel has not been set up")));
        return 0;
    }

    return 1;
}

void process_args_setup_channel(int& channel, int& direction, int& pud, int& initial,
                            const FunctionCallbackInfo<Value>& args)
{
  Isolate* isolate = args.GetIsolate();

  if(args.Length() < 2) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "setup() has 2 required arguments")));
    err();
  }

  if (!args[0]->IsNumber() || !args[1]->IsNumber()) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "setup() expected a number")));
    err();
  }

  channel = args[0]->NumberValue();
  direction = args[1]->NumberValue();

  if (direction != INPUT && direction != OUTPUT) {
      isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "An invalid direction was passed to setup()")));
      err();
  }

  if(args.Length() > 2) {
    if(args[2]->IsNumber()) {
      pud = args[2]->NumberValue();
    } else if(args[2]->IsUndefined()) {
      pud = PUD_OFF;
    } else {
      pud = -100; // spagetthi: fail the pud value check
    }
  }

  if (direction == OUTPUT)
      pud = PUD_OFF;

  if (pud != PUD_OFF && pud != PUD_DOWN && pud != PUD_UP) {
      isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "Invalid value for pull_up_down - should be either PUD_OFF, PUD_UP or PUD_DOWN")));
      err();
  }

  if(args.Length() > 3) {
    try {
      if(args[3]->IsNumber()) {
        initial = args[3]->NumberValue();
        if(initial != -1 || initial != 0 || initial != 1)
          err();
      } else if(!(args[3]->IsUndefined() || args[3]->IsNull())) {
        err();
      }
    } catch(const Error& e) {
      isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "Invalid value for initial")));
      throw;
    }
  }
}

// node function setup(channel, direction, pull_up_down=PUD_OFF, initial=null)
void
export_setup_channel(const FunctionCallbackInfo<Value>& args)
{
    int gpio, channel, direction;
    int pud = PUD_OFF;
    int initial = -1;
    int func;

    try {
      process_args_setup_channel(channel, direction, pud, initial, args);
    } catch(const Error& e) {
      return;
    }

    Isolate* isolate = args.GetIsolate();

    if ((gpio = channel_to_gpio(isolate, channel)) < 0)
        return;

    func = gpio_function(gpio);
    if (gpio_warnings &&                                      // warnings enabled and
         ((func != 0 && func != 1) ||                      // (already one of the alt functions or
         (gpio_direction[gpio] == -1 && func == 1)))  // already an output not set from this program)
    {
      fprintf(stderr, "%s\n", "RPIO.setup(): This channel is already in use, continuing anyway.  Use RPIO.setwarnings(False) to disable warnings.");
    }

//    printf("Setup GPIO %d direction %d pud %d\n", gpio, direction, pud);
    if (direction == OUTPUT && (initial == LOW || initial == HIGH)) {
//        printf("Writing intial value %d\n",initial);
        output_gpio(gpio, initial);
    }
    setup_gpio(gpio, direction, pud);
    gpio_direction[gpio] = direction;

}

void process_args_output_gpio(int& channel, int& value, const FunctionCallbackInfo<Value>& args)
{
  Isolate* isolate = args.GetIsolate();

  if(args.Length() < 2) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "setup() has 2 required arguments")));
    err();
  }

  if (!args[0]->IsNumber() || !args[1]->IsNumber()) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "setup() expected a number")));
    err();
  }

  channel = args[0]->NumberValue();
  value = args[1]->NumberValue();
}

// node function output(channel, value)
void
export_output_gpio(const FunctionCallbackInfo<Value>& args)
{
    int gpio, channel, value;

    Isolate* isolate = args.GetIsolate();

    try {
      process_args_output_gpio(channel, value, args);
    } catch(const Error& e) {
      return;
    }

    if ((gpio = channel_to_gpio(isolate, channel)) < 0)
        return;

    if (gpio_direction[gpio] != OUTPUT) {
        isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "The GPIO channel has not been set up as an OUTPUT")));
        return;
    }

//    printf("Output GPIO %d value %d\n", gpio, value);
    output_gpio(gpio, value);

}

// node function forceoutput(channel, value) without direction check
void
export_forceoutput_gpio(const FunctionCallbackInfo<Value>& args)
{
    int gpio, channel, value;

    Isolate* isolate = args.GetIsolate();

    try {
      process_args_output_gpio(channel, value, args);
    } catch(const Error& e) {
      return;
    }

//    printf("Output GPIO %d value %d\n", gpio, value);
    if ((gpio = channel_to_gpio(isolate, channel)) < 0)
        return;

    output_gpio(gpio, value);

}

// node function set_pullupdn(channel, pull_up_down=PUD_OFF)
static void
export_set_pullupdn(const FunctionCallbackInfo<Value>& args)
{
    int gpio, channel;
    int pud = PUD_OFF;

    Isolate* isolate = args.GetIsolate();

    if(args.Length() < 1) {
      isolate->ThrowException(Exception::TypeError(
          String::NewFromUtf8(isolate, "set_pullupdn() has 1 required argument")));
      return;
    }

    if (!args[0]->IsNumber()) {
      isolate->ThrowException(Exception::TypeError(
          String::NewFromUtf8(isolate, "channel must be a number")));
      return;
    }

    channel = args[0]->NumberValue();

    if(args.Length() > 1 && !args[1]->IsUndefined()) {
      if(!args[1]->IsNumber()) {
        isolate->ThrowException(Exception::TypeError(
            String::NewFromUtf8(isolate, "pull_up_down must be a number")));
        return;
      }

      pud = args[1]->NumberValue();
    }

    if ((gpio = channel_to_gpio(isolate, channel)) < 0)
        return;

    // printf("Setting gpio %d PULLUPDN to %d", gpio, pud);
    set_pullupdn(gpio, pud);

}

void process_args_input_gpio(int& channel, const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();

  if(args.Length() < 1) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "input() has 1 required argument")));
    return;
  }

  if (!args[0]->IsNumber()) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "channel must be a number")));
    return;
  }

  channel = args[0]->NumberValue();
}

// node function value = input(channel)
static void
export_input_gpio(const FunctionCallbackInfo<Value>& args)
{
    int gpio, channel;

    Isolate* isolate = args.GetIsolate();

    try {
      process_args_input_gpio(channel, args);
    } catch(const Error& e) {
      return;
    }

    if (!verify_input(isolate, channel, &gpio))
      return;

    //    printf("Input GPIO %d\n", gpio);
    if (input_gpio(gpio))
      args.GetReturnValue().Set(Number::New(isolate, 1));
    else
      args.GetReturnValue().Set(Number::New(isolate, 0));
}

// node function value = input(channel) without direction check
static void
export_forceinput_gpio(const FunctionCallbackInfo<Value>& args)
{
    int gpio, channel;

    Isolate* isolate = args.GetIsolate();

    try {
      process_args_input_gpio(channel, args);
    } catch(const Error& e) {
      return;
    }

    if ((gpio = channel_to_gpio(isolate, channel)) < 0)
        return;

    //    printf("Input GPIO %d\n", gpio);
    if (input_gpio(gpio))
      args.GetReturnValue().Set(Number::New(isolate, 1));
    else
      args.GetReturnValue().Set(Number::New(isolate, 0));
}

// node function setmode(mode)
static void
export_setmode(const FunctionCallbackInfo<Value>& args)
{
  Isolate* isolate = args.GetIsolate();

  if(args.Length() < 1) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "setmode() has 1 required argument")));
    return;
  }

  if (!args[0]->IsNumber()) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "mode must be a number")));
    return;
  }

  ::gpio_mode = args[0]->NumberValue();

  if (gpio_mode != BOARD && gpio_mode != BCM)
  {
      isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "An invalid mode was passed to setmode()")));
      return;
  }

}

// node function value = gpio_function(channel)
static void
export_gpio_function(const FunctionCallbackInfo<Value>& args)
{
    int gpio, channel, f;

    Isolate* isolate = args.GetIsolate();

    if(args.Length() < 1) {
      isolate->ThrowException(Exception::TypeError(
          String::NewFromUtf8(isolate, "gpio_function() has 1 required argument")));
      return;
    }

    if (!args[0]->IsNumber()) {
      isolate->ThrowException(Exception::TypeError(
          String::NewFromUtf8(isolate, "channel must be a number")));
      return;
    }

    channel = args[0]->NumberValue();

    if ((gpio = channel_to_gpio(isolate, channel)) < 0)
        return;

    f = gpio_function(gpio);
    switch (f)
    {
        case 0 : f = INPUT;  break;
        case 1 : f = OUTPUT; break;
    }
    args.GetReturnValue().Set(Number::New(isolate, f));
}

// node function setwarnings(state)
static void
export_setwarnings(const FunctionCallbackInfo<Value>& args)
{
  Isolate* isolate = args.GetIsolate();

  if(args.Length() < 1) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "gpio_function() has 1 required argument")));
    return;
  }

  if (!args[0]->IsBoolean()) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "channel must be a number")));
    return;
  }

  ::gpio_warnings = static_cast<int>(args[0]->BooleanValue());
}

// node function value = channel_to_gpio(channel)
static void
export_channel_to_gpio(const FunctionCallbackInfo<Value>& args)
{
  int channel, gpio;

  Isolate* isolate = args.GetIsolate();

  if(args.Length() < 1) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "channel_to_gpio() has 1 required argument")));
    return;
  }

  if (!args[0]->IsNumber()) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "channel must be a number")));
    return;
  }

  channel = args[0]->NumberValue();

  if ((gpio = channel_to_gpio(isolate, channel)) < 0)
      return;

  args.GetReturnValue().Set(Number::New(isolate, gpio));
}

// adapted from node.h
#define MY_NODE_DEFINE_STRING(target, constant)                                \
  do {                                                                        \
    v8::Isolate* isolate = target->GetIsolate();                              \
    v8::Local<v8::Context> context = isolate->GetCurrentContext();            \
    v8::Local<v8::String> constant_name =                                     \
        v8::String::NewFromUtf8(isolate, #constant);                          \
    v8::Local<v8::String> constant_value =                                    \
        v8::String::NewFromUtf8(isolate, constant);                           \
    v8::PropertyAttribute constant_attributes =                               \
        static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete);    \
    (target)->DefineOwnProperty(context,                                      \
                                constant_name,                                \
                                constant_value,                               \
                                constant_attributes).FromJust();              \
  }                                                                           \
while (0)

void init(Local<Object> exports, Local<Value> module, Local<Context> context) {
  Isolate* isolate = context->GetIsolate();

  isolate->AddGCPrologueCallback(cleanup);

  PWM::Init(exports);

  NODE_SET_METHOD(exports, "setup", export_setup_channel);
  NODE_SET_METHOD(exports, "output", export_output_gpio);
  NODE_SET_METHOD(exports, "forceoutput", export_forceoutput_gpio);
  NODE_SET_METHOD(exports, "set_pullupdn", export_set_pullupdn);
  NODE_SET_METHOD(exports, "input", export_input_gpio);
  NODE_SET_METHOD(exports, "forceinput", export_forceinput_gpio);
  NODE_SET_METHOD(exports, "setmode", export_setmode);
  NODE_SET_METHOD(exports, "gpio_function", export_gpio_function);
  NODE_SET_METHOD(exports, "setwarnings", export_setwarnings);
  NODE_SET_METHOD(exports, "channel_to_gpio", export_channel_to_gpio);

  define_constants(exports);


  // cache_rpi_revision();
  // switch (revision_int) {
  // case 1:
  //     pin_to_gpio = &pin_to_gpio_rev1;
  //     gpio_to_pin = &gpio_to_pin_rev1;
  //     break;
  // case 2:
  //     pin_to_gpio = &pin_to_gpio_rev2;
  //     gpio_to_pin = &gpio_to_pin_rev2;
  //     break;
  // case 3:
  //     pin_to_gpio = &pin_to_gpio_rev3;
  //     gpio_to_pin = &gpio_to_pin_rev3;
  //     break;
  // default:
  //     isolate->ThrowException(Exception::Error(
  //       String::NewFromUtf8(isolate, "This module can only be run on a Raspberry Pi!")));
  //     return;
  // }
  //
  // const int rpi_revision = revision_int;
  // NODE_DEFINE_CONSTANT(exports, rpi_revision);
  //
  // const char* rpi_revision_hex = revision_hex;
  // MY_NODE_DEFINE_STRING(exports, rpi_revision_hex);
  //
  // const char* version = "1.0.0";
  // MY_NODE_DEFINE_STRING(exports, version);
  //
  // module_setup(isolate);
}


// we use this instead of NODE_MODULE so we can use
// context->GetIsolate() in private functions such as module_setup()
NODE_MODULE_CONTEXT_AWARE(rpio, init)
