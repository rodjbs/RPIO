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

#include <node.h>
#include <node_object_wrap.h>

class PWMClass : public node::ObjectWrap {
public:
  // method used to export class
  static void Init(v8::Local<v8::Object> exports);

private:
  PWMClass();
  ~PWMClass();

  // js function PWM(channel, freqency)
  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);

  // object methods
  static void Start(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void ChangeDutyCycle(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void ChangeFrequency(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Stop(const v8::FunctionCallbackInfo<v8::Value>& args);

  unsigned int gpio_;
  float freq_;
  float dutycycle_;

  static v8::Persistent<v8::Function> constructor;
};
