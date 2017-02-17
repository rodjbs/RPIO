/*
Copyright (c) 2013 Ben Croston

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

#include "node_common.hh"
#include "node_pwm.hh"

extern "C" {
#include "soft_pwm.h"
#include "c_gpio.h"
}

using v8::String;
using v8::Local;
using v8::Object;
using v8::Isolate;
using v8::Exception;
using v8::FunctionTemplate;
using v8::Value;
using v8::Context;
using v8::Function;

PWMClass::PWMClass()
{
}

PWMClass::~PWMClass()
{
  pwm_stop(this->gpio_);
}

void PWMClass::Init(Local<Object> exports) {
  Isolate* isolate = exports->GetIsolate();

  // Prepare constructor template
  Local<FunctionTemplate> tpl = FunctionTemplate::New(isolate, New);
  tpl->SetClassName(String::NewFromUtf8(isolate, "PWM"));
  tpl->InstanceTemplate()->SetInternalFieldCount(3);

  // Prototype
  NODE_SET_PROTOTYPE_METHOD(tpl, "start", Start);
  NODE_SET_PROTOTYPE_METHOD(tpl, "changeDutyCycle", ChangeDutyCycle);
  NODE_SET_PROTOTYPE_METHOD(tpl, "changeFrequency", ChangeFrequency);
  NODE_SET_PROTOTYPE_METHOD(tpl, "stop", Stop);

  constructor.Reset(isolate, tpl->GetFunction());
  exports->Set(String::NewFromUtf8(isolate, "PWM"),
               tpl->GetFunction());
}

// js function PWM(channel, frequency)
void PWMClass::New(const v8::FunctionCallbackInfo<v8::Value>& args)
{
  int channel;
  float frequency;
  int gpio;

  Isolate* isolate = args.GetIsolate();

  if(args.Length() < 2) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "PWM() has 2 required arguments")));
    return;
  }

  if (!args[0]->IsNumber() || !args[1]->IsNumber()) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "PWM() expected numbers")));
    return;
  }

  channel = args[0]->NumberValue();
  frequency = args[1]->NumberValue();

  // convert channel to gpio
  if (get_gpio_number(isolate, channel, &gpio))
      return;

  // ensure channel set as output
  if (gpio_direction[gpio] != OUTPUT)
  {
    isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate,
      "You must setup() the GPIO channel as an output first")));
    return;
  }

  if (frequency <= 0.0)
  {
    isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate,
      "frequency must be greater than 0.0")));
    return;
  }

  if (args.IsConstructCall()) {
    // Invoked as constructor: `new PWM(...)`
    PWMClass* obj = new PWMClass();
    obj->Wrap(args.This());
    args.GetReturnValue().Set(args.This());

    obj->gpio_ = gpio;
    obj->freq_ = frequency;

    pwm_set_frequency(gpio, frequency);
  } else {
    // Invoked as plain function `PWM(...)`, turn into construct call.
    const int argc = 2;
    Local<Value> argv[argc] = { args[0], args[1] };
    Local<Context> context = isolate->GetCurrentContext();
    Local<Function> cons = Local<Function>::New(isolate, constructor);
    Local<Object> result =
       cons->NewInstance(context, argc, argv).ToLocalChecked();
    args.GetReturnValue().Set(result);
  }
}

// node method start(dutycycle)
void PWMClass::Start(const v8::FunctionCallbackInfo<v8::Value>& args)
{
  float dutycycle;

  Isolate* isolate = args.GetIsolate();

  if(args.Length() < 1) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "start() has 1 required arguments")));
    return;
  }

  if (!args[0]->IsNumber()) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "start() expected a number")));
    return;
  }

  dutycycle = args[0]->NumberValue();

  if (dutycycle < 0.0 || dutycycle > 100.0)
  {
    isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "dutycycle must have a value from 0.0 to 100.0")));
    return;
  }


  PWMClass* obj = ObjectWrap::Unwrap<PWMClass>(args.Holder());
  obj->dutycycle_ = dutycycle;
  pwm_set_duty_cycle(obj->gpio_, obj->dutycycle_);
  pwm_start(obj->gpio_);
}

// node method changeDutyCycle(dutycycle)
void PWMClass::ChangeDutyCycle(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    float dutycycle;

    Isolate* isolate = args.GetIsolate();

    if(args.Length() < 1) {
      isolate->ThrowException(Exception::TypeError(
          String::NewFromUtf8(isolate, "changeDutyCycle() has 1 required arguments")));
      return;
    }

    if (!args[0]->IsNumber()) {
      isolate->ThrowException(Exception::TypeError(
          String::NewFromUtf8(isolate, "changeDutyCycle() expected a number")));
      return;
    }

    dutycycle = args[0]->NumberValue();

    if (dutycycle < 0.0 || dutycycle > 100.0)
    {
        isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "dutycycle must have a value from 0.0 to 100.0")));
        return;
    }

    PWMClass* obj = ObjectWrap::Unwrap<PWMClass>(args.Holder());
    obj->dutycycle_ = dutycycle;
    pwm_set_duty_cycle(obj->gpio_, obj->dutycycle_);
}

// node method changeFrequency(frequency)
void PWMClass::ChangeFrequency(const v8::FunctionCallbackInfo<v8::Value>& args)
{
  float frequency;

  Isolate* isolate = args.GetIsolate();

  if(args.Length() < 1) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "changeFrequency() has 1 required arguments")));
    return;
  }

  if (!args[0]->IsNumber()) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "changeFrequency() expected a number")));
    return;
  }

  frequency = args[0]->NumberValue();

  if (frequency <= 0.0)
  {
    isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, "frequency must be greater than 0.0")));
    return;
  }

  PWMClass* obj = ObjectWrap::Unwrap<PWMClass>(args.Holder());

  obj->freq_ = frequency;

  pwm_set_frequency(obj->gpio_, obj->freq_);
}

// node method stop()
void PWMClass::Stop(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    PWMClass* obj = ObjectWrap::Unwrap<PWMClass>(args.Holder());
    pwm_stop(obj->gpio_);
}
