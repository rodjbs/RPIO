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

void define_constants(Local<Object> exports)
{
   high = HIGH;
   NODE_DEFINE_CONSTANT(exports, high);

   low = LOW;
   NODE_DEFINE_CONSTANT(exports, low);

   output = OUTPUT;
   NODE_DEFINE_CONSTANT(exports, output);

   input = INPUT;
   NODE_DEFINE_CONSTANT(exports, input);

   pwm = PWM;
   NODE_DEFINE_CONSTANT(exports, pwm);

   serial = SERIAL;
   NODE_DEFINE_CONSTANT(exports, serial);

   i2c = I2C;
   NODE_DEFINE_CONSTANT(exports, i2c);

   spi = SPI;
   NODE_DEFINE_CONSTANT(exports, spi);

   unknown = MODE_UNKNOWN;
   NODE_DEFINE_CONSTANT(exports, unknown);

   board = BOARD;
   NODE_DEFINE_CONSTANT(exports, board);

   bcm = BCM;
   NODE_DEFINE_CONSTANT(exports, bcm);

   pud_off = PUD_OFF + PY_PUD_CONST_OFFSET;
   NODE_DEFINE_CONSTANT(exports, pud_off);

   pud_up = PUD_UP + PY_PUD_CONST_OFFSET;
   NODE_DEFINE_CONSTANT(exports, pud_up);

   pud_down = PUD_DOWN + PY_PUD_CONST_OFFSET;
   NODE_DEFINE_CONSTANT(exports, pud_down);

   rising_edge = RISING_EDGE + PY_EVENT_CONST_OFFSET;
   NODE_DEFINE_CONSTANT(exports, rising_edge);

   falling_edge = FALLING_EDGE + PY_EVENT_CONST_OFFSET;
   NODE_DEFINE_CONSTANT(exports, falling_edge);

   both_edge = BOTH_EDGE + PY_EVENT_CONST_OFFSET;
   NODE_DEFINE_CONSTANT(exports, both_edge);
}
