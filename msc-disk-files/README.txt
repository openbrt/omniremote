OmniRemote
==========

This USB drive is exposed by the M5Stack StickS3 you have plugged in. The
stick is a universal IR remote that controls every AC in your home from a
single button.

Open README.html in any browser for full details.

----

Flash / configure / update
--------------------------

Use desktop Chrome or Microsoft Edge to open:

    https://openbrt.github.io/omniremote/             (中文)
    https://openbrt.github.io/omniremote/install-en.html  (English)

Keep USB-C plugged in. The page flashes / updates firmware directly to this
stick. No software install, the stick itself never goes online.


Daily use
---------

  Hold A (front big button) ........ pair a new AC; release on beep
  A short press .................... toggle every saved AC (broadcast)
  B short .......................... scroll menu
  B long ........................... run selected menu item
                                     (temp +/- , mode, fan)
  Hold A+B for 3 s ................. factory reset; wipe all profiles


Source / feedback
-----------------

https://github.com/openbrt/omniremote

If SCAN runs the full 90 seconds without locking, please file a GitHub
issue with your AC's brand and model — that helps the protocol library
grow.


中文摘要
--------

OmniRemote 是一根 M5Stack StickS3 万能空调遥控器。在 Chrome / Edge 打开
https://openbrt.github.io/omniremote/ 即可烧录 / 升级固件。

  长按 A   配对新空调,听到嘀声松手
  A 短按   一键开关所有已配空调(toggle)
  B 短按   滚菜单, B 长按  执行(温度/模式/风速)
  A+B 长按 3 秒  清空所有 profile

源码: https://github.com/openbrt/omniremote
