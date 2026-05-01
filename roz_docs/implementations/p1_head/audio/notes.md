Wired two Adafruit speakers (P3968B) to the Adafruit MAX98306 amplifier. 
https://www.adafruit.com/product/3968?srsltid=AfmBOoqHMF6Fuo4KhQAABdfvXpRvqvWKraY1qZawjJc07EML4CITDTW2
https://www.adafruit.com/product/987?srsltid=AfmBOoq_Js7YYsGPwtXGOL8OluN8SXIOWGZCzjqDBYg24a-tgkLoTHCc

Left the jumper off (6dB gain default)
Breadboard the amp w/ L- and R- to GND as noted in amp datasheet
3.5mm audio cable stripped. White -> R+, Red -> L+, Black -> GND
3.5mm audio plug into Cubilux DAC sound card.

"Cubilux USB to 3.5mm Audio Adapter, Hi-Res 384KHz/32-bit USB A DAC Dongle, UAC 2.0 External Sound Card for PC Laptop MacBook, Windows 11/10, macOS, Linux"

https://www.amazon.com/Cubilux-Adapter-External-MacBook-Windows/dp/B0GJ4Q9LD7/ref=sr_1_4?crid=19UE63PD58KQ6&dib=eyJ2IjoiMSJ9.ksGDKiWXHy4jI_-w-zKAHY1RipvnjeWd18ZgDrQonK2kQ9PvmqJTcfn2jb95i_iUoeBiHMmZd_jXSq1dx7OLCVMAt0o4X_1NWNCa7Fd9Lfdv1OpI7Z_gf4pEzv-Mws7TJHTWwW-G7xmOPYZT-uK3p4sAUwzDrToIHSv3J5WElL14oN3f8tKR7ajfiMdavFYYjog3myhg2L4YqWjsTY_8d-v47k5SyO79YEv8-sfFB_gEr75AfOg4CbvpNk5ZIsb3DdX9nY0fEqAhy2DYP8vGx4VLEcuhUpsU-zX14zSFnDI.SeQn8647C2r50dfD01eMmb_bqFdrYEHkLn1xbPRgPCU&dib_tag=se&keywords=cubilux+usb+a+to+audio+adapter&qid=1777514792&s=electronics&sbo=RZvfv%2F%2FHxDF%2BO5021pAnSA%3D%3D&sprefix=cubilux+usb+a+to+audio+adapte%2Celectronics%2C137&sr=1-4


Breadboard Jetson 40-pin header pin 2 or 4 5V to amp VDD, pin 6 to amp GND

https://developer.download.nvidia.com/assets/embedded/secure/jetson/orin_nano/docs/Jetson-Orin-Nano-DevKit-Carrier-Board-Specification_SP-11324-001_v1.3.pdf?t=eyJscyI6ImdzZW8iLCJsc2QiOiJodHRwczovL3d3dy5nb29nbGUuY29tLyJ9&__token__=exp=1777515078~hmac=041c64b02632c3b1e71b61bc2421fd06535b6eafc9d974a83d9cfa78e439b0d0


NOTE: Do not unplug speakers w/ live amp. Risk of damaging amplifier.

Power up Jetson:
On Wilson's network the Jetson IP lease is 192.168.1.160

```
    mwilson@jetson:~$ lsusb
    Bus 002 Device 002: ID 0bda:0489 Realtek Semiconductor Corp. 4-Port USB 3.0 Hub
    Bus 002 Device 001: ID 1d6b:0003 Linux Foundation 3.0 root hub
    Bus 001 Device 003: ID 13d3:3549 IMC Networks Bluetooth Radio
    Bus 001 Device 004: ID 31b2:f020 KTMicro Cubilux HA-3
    Bus 001 Device 002: ID 0bda:5489 Realtek Semiconductor Corp. 4-Port USB 2.0 Hub
    Bus 001 Device 001: ID 1d6b:0002 Linux Foundation 2.0 root hub

    mwilson@jetson:~$ aplay -l
    **** List of PLAYBACK Hardware Devices ****
    card 0: HA3 [Cubilux HA-3], device 0: USB Audio [USB Audio]
      Subdevices: 1/1
      Subdevice #0: subdevice #0

    mwilson@jetson:~$ aplay -D plughw:0,0 /usr/share/sounds/alsa/Front_Center.wav
    Playing WAVE '/usr/share/sounds/alsa/Front_Center.wav' : Signed 16 bit Little Endian, Rate 48000 Hz, Mono

    ## Test Left
    mwilson@jetson:~$ speaker-test -D plughw:0,0 -c 2 -s 1 -t sine -f 440 -l 1

    speaker-test 1.2.6

    Playback device is plughw:0,0
    Stream parameters are 48000Hz, S16_LE, 2 channels
    Sine wave rate is 440.0000Hz
    Rate set to 48000Hz (requested 48000Hz)
    Buffer size range from 32 to 96000
    Period size range from 16 to 48000
    Using max buffer size 96000
    Periods = 4
    was set period_size = 24000
    was set buffer_size = 96000
      - Front Left

    ## Test Right
    mwilson@jetson:~$ speaker-test -D plughw:0,0 -c 2 -s 2 -t sine -f 440 -l 1

    speaker-test 1.2.6

    Playback device is plughw:0,0
    Stream parameters are 48000Hz, S16_LE, 2 channels
    Sine wave rate is 440.0000Hz
    Rate set to 48000Hz (requested 48000Hz)
    Buffer size range from 32 to 96000
    Period size range from 16 to 48000
    Using max buffer size 96000
    Periods = 4
    was set period_size = 24000
    was set buffer_size = 96000
      - Front Right

    ## Calibrate volume using two terminals
    # Terminal 1 - without -l test is 4 seconds
    mwilson@jetson:~$ speaker-test -D plughw:CARD=0,DEV=0 -c 2 -s 1 -t sine -f 440

    # Terminal 2 - use arrow keys to adjust volume
    mwilson@jetson:~$ alsamixer -c 0

    # Do the same for right speaker (-s 2)
    # Do the same using the aplay 'Front Center' wave file
    # Adjust the volume to find the high end w/ no audio fuzz or clipping and no hiss when there is no audio
    # Use the amp jumper to increase the gain to find the high end
    # Adjust the volume to find the low end where audio is understandable using the 'Front Center' wave file

    # 4/29/26 Amp gain jumper setting 12dB, alsamixer high-end PCM 72 (-7dB), low-end PCM 6 (-42dB)
    mwilson@jetson:~$ for f in /usr/share/sounds/alsa/*.wav ; do aplay -D plughw:0,0 $f ; sleep 1 ; done

    # Use these commands to set the volume
    amixer -c <cardnum> sset PCM 75%      # percent
    amixer -c <cardnum> sset PCM 6dB<+->  # or in dB
    amixer -c <cardnum> sget PCM          # read back current setting


    ## Power down, connect the JMTek USB audio adapter to the Jetson
    # Strip and plug in the 3.5mm audio and connect to the microphone (black GND terminal 2, red mic output terminal 1)
    "Electret Microphone - 20Hz-20KHz Omnidirectional"
    https://www.adafruit.com/product/1064?srsltid=AfmBOopVPVGvhRwWhO7gG3sKRzFJ7ftudgp22U4EOnfAt9Wx3drM5nl3
    https://cdn-shop.adafruit.com/datasheets/CMA-4544PF-W.pdf
    "Plugable USB Audio Adapter with 3.5mm Speaker-Headphone and Microphone Jack, Add an External Stereo Sound Card to Any PC, Compatible with Windows, Mac, and Linux - Driverless"
    https://www.amazon.com/Plugable-Headphone-Microphone-Aluminum-Compatible/dp/B00NMXY2MO

    mwilson@jetson:~$ lsusb
    ...
    Bus 001 Device 004: ID 0c76:120b JMTek, LLC. Plugable USB Audio Device
    ...

    # NOTE: Now the Cubilux USB audio for the speakers is on card 1, device 0 so the commands are different.
    # Check the card and device w/ arecord -l and aplay -l to find the correct audio card and device

    mwilson@jetson:~$ arecord -l
    **** List of CAPTURE Hardware Devices ****
    card 0: Device [Plugable USB Audio Device], device 0: USB Audio [USB Audio]
      Subdevices: 1/1
      Subdevice #0: subdevice #0
    card 1: HA3 [Cubilux HA-3], device 0: USB Audio [USB Audio]
      Subdevices: 1/1
      Subdevice #0: subdevice #0
    ...

    # record a wave file and play it back
    mwilson@jetson:~$ arecord -D plughw:0,0 -V mono -f S16_LE -r 16000 -c 1 -d 10 /tmp/tmp.wav
    mwilson@jetson:~$ aplay -D plughw:1,0 /tmp/tmp.wav


    # noise, clicking, and need to speak closely, directly into the microphone, but it works

    # checking for mic capabilities
    amixer -c <micname>                                  # list all controls
    amixer -c <micname> sset Mic 80%                     # bump capture level
    amixer -c <micname> sset 'Auto Gain Control' on      # if AGC exists
    amixer -c <micname> sset 'Mic Boost' '20dB'          # if a boost control exists

    The plugable USB audio adapter doesn't have AGC or Mic Boost.
    Setting the Mic to 100% didn't have any noticeable improvement.

```
