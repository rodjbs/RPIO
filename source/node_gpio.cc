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
   int gpio;

   Isolate* isolate = args.GetIsolate();

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
         if (get_gpio_number(isolate, args[0]->NumberValue(), &gpio))
            return;

        // clean up any /sys/class exports
        event_cleanup(gpio);

        // set everything back to input
        if (gpio_direction[gpio] != -1) {
           setup_gpio(gpio, INPUT, PUD_OFF);
           gpio_direction[gpio] = -1;
           found = 1;
        }
      } else {
        isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "Channel must be a number")));
        return;
      }
    }

    if (!found && gpio_warnings) {
      fprintf(stderr, "No channels have been set up yet - nothing to clean up!  Try cleaning up at the end of your program instead!\n");
    }
}

void gc_cleanup(Isolate *isolate, v8::GCType type, v8::GCCallbackFlags flags)
{
    // TODO call export_cleanup
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
      pud = pud_off;
    } else {
      pud = -100; // spagetthi: fail the pud value check
    }
  } else {
    pud = pud_off;
  }

  int invalid_initial = 0;
  if(args.Length() > 3) {
    if(args[3]->IsNumber()) {
      initial = args[3]->NumberValue();
      if(initial != -1 && initial != 0 && initial != 1)
        invalid_initial = 1;
    } else if(!(args[3]->IsUndefined() || args[3]->IsNull())) {
      invalid_initial = 1;
    }
  }

  if(invalid_initial) {
    isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Invalid value for initial")));
    return 1;
  }

  return 0;
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

    if (mmap_gpio_mem(isolate))
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

    if (get_gpio_number(isolate, channel, &gpio))
       return;

    func = gpio_function(gpio);
    if (gpio_warnings &&                             // warnings enabled and
        ((func != 0 && func != 1) ||                 // (already one of the alt functions or
        (gpio_direction[gpio] == -1 && func == 1)))  // already an output not set from this program)
    {
       fprintf(stderr, "This channel is already in use, continuing anyway.  Use setwarnings(false) to disable warnings.\n");
    }

    // warn about pull/up down on i2c channels
    if (gpio_warnings) {
       if (rpiinfo.p1_revision == 0) { // compute module - do nothing
       } else if ((rpiinfo.p1_revision == 1 && (gpio == 0 || gpio == 1)) ||
                  (gpio == 2 || gpio == 3)) {
          if (pud == PUD_UP || pud == PUD_DOWN)
             fprintf(stderr, "A physical pull up resistor is fitted on this channel!\n");
       }
    }

    if (direction == OUTPUT && (initial == LOW || initial == HIGH)) {
       output_gpio(gpio, initial);
    }
    setup_gpio(gpio, direction, pud);
    gpio_direction[gpio] = direction;

}

int process_args_output_gpio(int& channel, int& value, const FunctionCallbackInfo<Value>& args)
{
  Isolate* isolate = args.GetIsolate();

  if(args.Length() < 2) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "output() has 2 required arguments")));
    return 1;
  }

  if (!args[0]->IsNumber() || !args[1]->IsNumber()) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "output() expected a number")));
    return 1;
  }

  channel = args[0]->NumberValue();
  value = args[1]->NumberValue();

  return 0;
}

// node function output(channel, value)
void
export_output_gpio(const FunctionCallbackInfo<Value>& args)
{
    int gpio, channel, value;

    Isolate* isolate = args.GetIsolate();

    if(process_args_output_gpio(channel, value, args))
      return;

    //    printf("Output GPIO %d value %d\n", gpio, value);
    if (get_gpio_number(isolate, channel, &gpio))
        return;

    if (gpio_direction[gpio] != OUTPUT)
    {
       isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "The GPIO channel has not been set up as an OUTPUT")));
       return;
    }

    if (check_gpio_priv(isolate))
       return;

    output_gpio(gpio, value);

}

int process_args_input_gpio(int& channel, const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();

  if(args.Length() < 1) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "input() has 1 required argument")));
    return 1;
  }

  if (!args[0]->IsNumber()) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "channel must be a number")));
    return 1;
  }

  channel = args[0]->NumberValue();

  return 0;
}

// node function value = input(channel)
static void
export_input_gpio(const FunctionCallbackInfo<Value>& args)
{
    int gpio, channel;

    Isolate* isolate = args.GetIsolate();

    if(process_args_input_gpio(channel, args))
      return;

    if (get_gpio_number(isolate, channel, &gpio))
        return;

    // check channel is set up as an input or output
    if (gpio_direction[gpio] != INPUT && gpio_direction[gpio] != OUTPUT)
    {
       isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "You must setup() the GPIO channel first")));
       return;
    }

    if (check_gpio_priv(isolate))
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

  int new_mode = args[0]->NumberValue();

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
  Isolate* isolate = args.GetIsolate();

   if (setup_error)
   {
      isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "Module not imported correctly!")));
      return;
   }

   if (gpio_mode == MODE_UNKNOWN)
      return;

   args.GetReturnValue().Set(Number::New(isolate, gpio_mode));
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

    if (get_gpio_number(isolate, channel, &gpio))
        return;

    if (mmap_gpio_mem(isolate))
       return;

    f = gpio_function(gpio);
    switch (f)
    {
       case 0 : f = INPUT;  break;
       case 1 : f = OUTPUT; break;

       // ALT 0
       case 4 : switch (gpio)
                {
                   case 0 :
                   case 1 :
                   case 2 :
                   case 3 : f = I2C; break;

                   case 7 :
                   case 8 :
                   case 9 :
                   case 10 :
                   case 11 : f = SPI; break;

                   case 12 :
                   case 13 : f = PWM; break;

                   case 14 :
                   case 15 : f = SERIAL; break;

                   case 28 :
                   case 29 : f = I2C; break;

                   default : f = MODE_UNKNOWN; break;
                }
                break;

       // ALT 5
       case 2 : if (gpio == 18 || gpio == 19) f = PWM; else f = MODE_UNKNOWN;
                break;

       // ALT 4
       case 3 : switch (gpio)

                {
                   case 16 :
                   case 17 :
                   case 18 :
                   case 19 :
                   case 20 :
                   case 21 : f = SPI; break;
                   default : f = MODE_UNKNOWN; break;
                }
                break;

       default : f = MODE_UNKNOWN; break;

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
        String::NewFromUtf8(isolate, "argument must be boolean")));
    return;
  }

  if (setup_error)
  {
     isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "Module not imported correctly!")));
     return;
  }

  gpio_warnings = static_cast<int>(args[0]->BooleanValue());
}

static int chan_from_gpio(int gpio)
{
   int chan;
   int chans;

   if (gpio_mode == BCM)
      return gpio;
   if (rpiinfo.p1_revision == 0)   // not applicable for compute module
      return -1;
   else if (rpiinfo.p1_revision == 1 || rpiinfo.p1_revision == 2)
      chans = 26;
   else
      chans = 40;
   for (chan=1; chan<=chans; chan++)
      if (*(*pin_to_gpio+chan) == gpio)
         return chan;
   return -1;
}

// TODO transcribe edge / event methods



void init(Local<Object> exports, Local<Value> module, Local<Context> context) {
  Isolate* isolate = context->GetIsolate();

  isolate->AddGCPrologueCallback(gc_cleanup);

  PWMClass::Init(exports);

  NODE_SET_METHOD(exports, "cleanup", export_cleanup);
  NODE_SET_METHOD(exports, "setup", export_setup_channel);
  NODE_SET_METHOD(exports, "output", export_output_gpio);
  NODE_SET_METHOD(exports, "input", export_input_gpio);
  NODE_SET_METHOD(exports, "setmode", export_setmode);
  NODE_SET_METHOD(exports, "getmode", export_getmode);
  NODE_SET_METHOD(exports, "gpio_function", export_gpio_function);
  NODE_SET_METHOD(exports, "setwarnings", export_setwarnings);

  define_constants(exports);

  for (int i=0; i<54; i++)
     gpio_direction[i] = -1;

  // detect board revision and set up accordingly
  if (get_rpi_info(&rpiinfo))
  {
     isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "This module can only be run on a Raspberry Pi!")));
     setup_error = 1;
     return;
  }


   if (rpiinfo.p1_revision == 1) {
      pin_to_gpio = &pin_to_gpio_rev1;
   } else if (rpiinfo.p1_revision == 2) {
      pin_to_gpio = &pin_to_gpio_rev2;
   } else { // assume model B+ or A+ or 2B
      pin_to_gpio = &pin_to_gpio_rev3;
   }

  int rpi_revision = rpiinfo.p1_revision;
  NODE_DEFINE_CONSTANT(exports, rpi_revision);

}


// we use this instead of NODE_MODULE so we can use
// context->GetIsolate() in private functions such as module_setup()
NODE_MODULE_CONTEXT_AWARE(rpio, init)
