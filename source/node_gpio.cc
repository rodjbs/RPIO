/*
Based on RPi.GPIO by Ben Croston

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <node.h>

#include "node_constants.hh"
#include "node_common.hh"
#include "node_pwm.hh"

extern "C" {
#include "c_gpio.h"
#include "event_gpio.h"
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

static int rpi_revision; // deprecated
static int board_info;
static int gpio_warnings = 1;

static int mmap_gpio_mem(Isolate* isolate)
{
   int result;

   if (module_setup)
      return 0;

   result = setup();
   if (result == SETUP_DEVMEM_FAIL)
   {
      isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "No access to /dev/mem.  Try running as root!")));
      return 1;
   } else if (result == SETUP_MALLOC_FAIL) {
      isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "No memory")));
      return 2;
   } else if (result == SETUP_MMAP_FAIL) {
      isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "Mmap of GPIO registers failed")));
      return 3;
   } else if (result == SETUP_CPUINFO_FAIL) {
      isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "Unable to open /proc/cpuinfo")));
      return 4;
   } else if (result == SETUP_NOT_RPI_FAIL) {
      isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "Not running on a RPi!")));
      return 5;
   } else { // result == SETUP_OK
      module_setup = 1;
      return 0;
   }
}

// node function cleanup(channel?)
static void export_cleanup(const FunctionCallbackInfo<Value>& args)
{
   int i;
   int found = 0;
   unsigned int gpio;

   void cleanup_one(void)
   {
      // clean up any /sys/class exports
      event_cleanup(gpio);

      // set everything back to input
      if (gpio_direction[gpio] != -1) {
         setup_gpio(gpio, INPUT, PUD_OFF);
         gpio_direction[gpio] = -1;
         found = 1;
      }
   }

   if (module_setup && !setup_error) {
      if (args[0]->IsUndefined()) {   // channel not set - cleanup everything
         // clean up any /sys/class exports
         event_cleanup_all();

         // set everything back to input
         for (i=0; i<54; i++) {
            if (gpio_direction[i] != -1) {
               setup_gpio(i, INPUT, PUD_OFF);
               gpio_direction[i] = -1;
               found = 1;
            }
         }
         gpio_mode = MODE_UNKNOWN;
      } else if (args[0]->IsNumber()) {    // channel was an int indicating single channel
         if (get_gpio_number(args[0]->NumberValue(), &gpio))
            return;
         cleanup_one();
      } else {
        isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "Channel must be a number")));
        return;
      }
    }

    if (!found && gpio_warnings) {
      fprintf(stderr, "No channels have been set up yet - nothing to clean up!  Try cleaning up at the end of your program instead!");
    }
}

int process_args_setup_channel(int& channel, int& direction, int& pud, int& initial,
                            const FunctionCallbackInfo<Value>& args)
{
  Isolate* isolate = args.GetIsolate();

  if(args.Length() < 2) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "setup() has 2 required arguments")));
    return 1;
  }

  if (!args[0]->IsNumber() || !args[1]->IsNumber()) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "setup() expected a number")));
    return 1;
  }

  channel = args[0]->NumberValue();
  direction = args[1]->NumberValue();

  if (direction != INPUT && direction != OUTPUT) {
      isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "An invalid direction was passed to setup()")));
      return 1;
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

  if(args.Length() > 3) {
    try {
      if(args[3]->IsNumber()) {
        initial = args[3]->NumberValue();
        if(initial != -1 || initial != 0 || initial != 1)
          return 1;
      } else if(!(args[3]->IsUndefined() || args[3]->IsNull())) {
        return 1;
      }
    } catch(const Error& e) {
      isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "Invalid value for initial")));
      throw;
    }
  }
}

// node function setup(channel, direction, pull_up_down=PUD_OFF, initial=undefined)
void
export_setup_channel(const FunctionCallbackInfo<Value>& args)
{
    int gpio, channel, direction;
    int pud = PUD_OFF;
    int initial = -1;
    int func;

    if(process_args_setup_channel(channel, direction, pud, initial, args))
      return;

    Isolate* isolate = args.GetIsolate();

    // check module has been imported cleanly
    if (setup_error)
    {
       isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "Module not imported correctly!")));
       return;
    }

    if (mmap_gpio_mem())
       return;

    if (direction != INPUT && direction != OUTPUT) {
       isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "An invalid direction was passed to setup()")));
       return;
    }

    if (direction == OUTPUT && pud != PUD_OFF + PY_PUD_CONST_OFFSET) {
       isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "pull_up_down parameter is not valid for outputs")));
       return;
    }

    if (direction == INPUT && initial != -1) {
       isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "initial parameter is not valid for inputs")));
       return;
    }

    if (direction == OUTPUT)
       pud = PUD_OFF + PY_PUD_CONST_OFFSET;

    pud -= PY_PUD_CONST_OFFSET;
    if (pud != PUD_OFF && pud != PUD_DOWN && pud != PUD_UP) {
       isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "Invalid value for pull_up_down - should be either PUD_OFF, PUD_UP or PUD_DOWN")));
       return;
    }

    if (get_gpio_number(channel, &gpio))
       return;

    func = gpio_function(gpio);
    if (gpio_warnings &&                             // warnings enabled and
        ((func != 0 && func != 1) ||                 // (already one of the alt functions or
        (gpio_direction[gpio] == -1 && func == 1)))  // already an output not set from this program)
    {
       fprintf(stderr, "This channel is already in use, continuing anyway.  Use setwarnings(false) to disable warnings.");
    }

    // warn about pull/up down on i2c channels
    if (gpio_warnings) {
       if (rpiinfo.p1_revision == 0) { // compute module - do nothing
       } else if ((rpiinfo.p1_revision == 1 && (gpio == 0 || gpio == 1)) ||
                  (gpio == 2 || gpio == 3)) {
          if (pud == PUD_UP || pud == PUD_DOWN)
             fprintf(stderr, "A physical pull up resistor is fitted on this channel!");
       }
    }

    if (direction == OUTPUT && (initial == LOW || initial == HIGH)) {
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
    return 1;
  }

  if (!args[0]->IsNumber() || !args[1]->IsNumber()) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "setup() expected a number")));
    return 1;
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

    if(process_args_output_gpio(channel, value, args))
      return 1;

    //    printf("Output GPIO %d value %d\n", gpio, value);
    if (get_gpio_number(channel, &gpio))
        return;

    if (gpio_direction[gpio] != OUTPUT)
    {
       isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "The GPIO channel has not been set up as an OUTPUT")));
       return;
    }

    if (check_gpio_priv())
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

    if(process_args_input_gpio(channel, args))
      return;

    if (get_gpio_number(channel, &gpio))
        return;

    // check channel is set up as an input or output
    if (gpio_direction[gpio] != INPUT && gpio_direction[gpio] != OUTPUT)
    {
       isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "You must setup() the GPIO channel first")));
       return;
    }

    if (check_gpio_priv())
       return;

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

  int new_mode = args[0]->NumberVaue();

  if (gpio_mode != MODE_UNKNOWN && new_mode != gpio_mode)
  {
     isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "A different mode has already been set!")));
     return;
  }

  if (setup_error)
  {
     isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "Module not imported correctly!")));
     return;
  }

  if (new_mode != BOARD && new_mode != BCM)
  {
     isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "An invalid mode was passed to setmode()")));
     return;
  }

  if (rpiinfo.p1_revision == 0 && new_mode == BOARD)
  {
     isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "BOARD numbering system not applicable on compute module")));
     return;
  }

  gpio_mode = new_mode;

}

// node function mode = getmode()
void export_getmode(const FunctionCallbackInfo<Value>& args)
{
   if (setup_error)
   {
      isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "Module not imported correctly!")));
      return;
   }

   if (gpio_mode == MODE_UNKNOWN)
      return;

   args.GetReturnValue().Set(Number::New(gpio_mode, 0));
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

static unsigned int chan_from_gpio(unsigned int gpio)
{
   int chan;
   int chans;

   if (gpio_mode == BCM)
      return;
   if (rpiinfo.p1_revision == 0)   // not applicable for compute module
      return;
   else if (rpiinfo.p1_revision == 1 || rpiinfo.p1_revision == 2)
      chans = 26;
   else
      chans = 40;
   for (chan=1; chan<=chans; chan++)
      if (*(*pin_to_gpio+chan) == gpio)
         return;
   return;
}

// adapted from node.h
// #define MY_NODE_DEFINE_STRING(target, constant)                                \
//   do {                                                                        \
//     v8::Isolate* isolate = target->GetIsolate();                              \
//     v8::Local<v8::Context> context = isolate->GetCurrentContext();            \
//     v8::Local<v8::String> constant_name =                                     \
//         v8::String::NewFromUtf8(isolate, #constant);                          \
//     v8::Local<v8::String> constant_value =                                    \
//         v8::String::NewFromUtf8(isolate, constant);                           \
//     v8::PropertyAttribute constant_attributes =                               \
//         static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete);    \
//     (target)->DefineOwnProperty(context,                                      \
//                                 constant_name,                                \
//                                 constant_value,                               \
//                                 constant_attributes).FromJust();              \
//   }                                                                           \
// while (0)

void init(Local<Object> exports, Local<Value> module, Local<Context> context) {
  Isolate* isolate = context->GetIsolate();

  isolate->AddGCPrologueCallback(cleanup);

  PWM::Init(exports);

  NODE_SET_METHOD(exports, "cleanup", export_cleanup);
  NODE_SET_METHOD(exports, "setup", export_setup_channel);
  NODE_SET_METHOD(exports, "output", export_output_gpio);
  NODE_SET_METHOD(exports, "set_pullupdn", export_set_pullupdn);
  NODE_SET_METHOD(exports, "input", export_input_gpio);
  NODE_SET_METHOD(exports, "setmode", export_setmode);
  NODE_SET_METHOD(exports, "getmode", export_getmode);
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
