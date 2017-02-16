{
  "targets": [
    {
      "target_name": "rpio",
      "sources": [
        "source/node_gpio.cc",
        "source/c_gpio.c",
        "source/cpuinfo.c",
        "source/common.c",
        "source/constants.c",
        "source/event_gpio.c",
        "source/soft_pwm.c"
        ],
      'cflags!': [ '-fno-exceptions' ],
      'cflags_cc!': [ '-fno-exceptions' ]
    }
  ]
}
