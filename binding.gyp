{
  "targets": [
    {
      "target_name": "rpio",
      "sources": [
        "source/node_gpio.cc",
        "source/node_common.cc",
        "source/node_constants.cc",
        "source/node_pwn.cc",
        "source/c_gpio.c",
        "source/cpuinfo.c",
        "source/event_gpio.c",
        "source/soft_pwm.c"
        ],
      'cflags!': [ '-fno-exceptions' ],
      'cflags_cc!': [ '-fno-exceptions' ]
    }
  ]
}
