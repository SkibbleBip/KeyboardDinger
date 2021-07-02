# KeyboardDinger
Service Daemon that adds audio notification functionality when the caps lock and other toggle keys are pressed on the keyboard for GNU-Linux

When caps lock is enabled, an audible "Ding" sound is played, and when caps lock is disabled, an audible "Dong" is played. This gives the user 
audible feedback when typing on the keyboard and pressing the caps lock key.

Sound is played using the ALSA API and the application consists of a server and a client. The server runs as a root service/daemon and is used
to scan the keyboard event file for caps lock inputs, and sends data to a FIFO pipe which is read by the client. The client runs at user level
and checks the data sent by the FIFO pipe and plays sounds in accordance to what state the caps lock is in.

~~Currently only Caps Lock is supported, in the future I may add Num Lock and Scroll Lock support.
I also plan on making the server also compatable with being compiled as a kernel module.~~ 
I have completed the ability to use it for all the lock keys including scroll and num lock, however I need someone to test out the scroll lock
dinging because apparently my scroll lock on my pc doesn't work and I have no other keyboards with a scroll lock.
