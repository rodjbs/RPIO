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

extern "C" {
#include "c_gpio.h"
#include "event_gpio.h"
}

int high;
int low;
int input;
int output;
int pwm;
int serial;
int i2c;
int spi;
int unknown;
int board;
int bcm;
int pud_off;
int pud_up;
int pud_down;
int rising_edge;
int falling_edge;
int both_edge;

// adapted from node.h
#define MY_DEFINE_CONSTANT(target, constant, str)                             \
  do {                                                                        \
    v8::Isolate* isolate = target->GetIsolate();                              \
    v8::Local<v8::Context> context = isolate->GetCurrentContext();            \
    v8::Local<v8::String> constant_name =                                     \
        v8::String::NewFromUtf8(isolate, str);                                \
    v8::Local<v8::Number> constant_value =                                    \
        v8::Number::New(isolate, static_cast<double>(constant));              \
    v8::PropertyAttribute constant_attributes =                               \
        static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete);    \
    (target)->DefineOwnProperty(context,                                      \
                                constant_name,                                \
                                constant_value,                               \
                                constant_attributes).FromJust();              \
  }                                                                           \
while (0)

void define_constants(const v8::Local<v8::Object>& exports)
{
   high = HIGH;
   MY_DEFINE_CONSTANT(exports, high, "HIGH");

   low = LOW;
   MY_DEFINE_CONSTANT(exports, low, "LOW");

   output = OUTPUT;
   MY_DEFINE_CONSTANT(exports, output, "OUTPUT");

   input = INPUT;
   MY_DEFINE_CONSTANT(exports, input, "INPUT");

   pwm = PWM;
   MY_DEFINE_CONSTANT(exports, pwm, "HARD_PWM");

   serial = SERIAL;
   MY_DEFINE_CONSTANT(exports, serial, "SERIAL");

   i2c = I2C;
   MY_DEFINE_CONSTANT(exports, i2c, "I2C");

   spi = SPI;
   MY_DEFINE_CONSTANT(exports, spi, "SPI");

   unknown = MODE_UNKNOWN;
   MY_DEFINE_CONSTANT(exports, unknown, "UNKNOWN");

   board = BOARD;
   MY_DEFINE_CONSTANT(exports, board, "BOARD");

   bcm = BCM;
   MY_DEFINE_CONSTANT(exports, bcm, "BCM");

   pud_off = PUD_OFF + PY_PUD_CONST_OFFSET;
   MY_DEFINE_CONSTANT(exports, pud_off, "PUD_OFF");

   pud_up = PUD_UP + PY_PUD_CONST_OFFSET;
   MY_DEFINE_CONSTANT(exports, pud_up, "PUD_UP");

   pud_down = PUD_DOWN + PY_PUD_CONST_OFFSET;
   MY_DEFINE_CONSTANT(exports, pud_down, "PUD_DOWN");

   rising_edge = RISING_EDGE + PY_EVENT_CONST_OFFSET;
   MY_DEFINE_CONSTANT(exports, rising_edge, "RISING_EDGE");

   falling_edge = FALLING_EDGE + PY_EVENT_CONST_OFFSET;
   MY_DEFINE_CONSTANT(exports, falling_edge, "FALLING_EDGE");

   both_edge = BOTH_EDGE + PY_EVENT_CONST_OFFSET;
   MY_DEFINE_CONSTANT(exports, both_edge, "BOTH_EDGE");
}
