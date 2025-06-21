## DRM driver for Amlogic meson64(G12) platform with cursor plane.  

### Warning
・Interlace mode is not implemented in this driver.  
&emsp; So this driver ***DOES NOT WORK with CVBS output***, and I have no plan to fix it.  
&emsp; Because I have no display except an ordinally HDMI LCD display, so I cannot test the feature.  
・Not sure whether this one works with MIPI/DSI output.  
・Not sure that this one works with other resolution except 1920x1080, especially with higher resolutions.  
・I have used this driver for more than 1 year(in June 2025), and there has been no big probrem  
&emsp; except sometime 'the ghost' had appeard on shutdown screen.  
&emsp;But ***there is still NO WRRANTY AT ALL.***   

### Note
・At first I referred to tobetter's odroid-5.14.y, but finally I took a different approach.  
> https://github.com/tobetter/linux/tree/odroid-5.14.y  

・This driver is not implemented for older platforms than G12A, because I have no board.  
&emsp; But I think you can easily update this one for these platforms. 

・I recommend to use the 6.6 kernel or newer ones. Because when I tested the 5.15.150 kernel with my driver,  
&emsp; it crashed so often( I believe it was not because of my driver... ).   
&emsp; So I got to use the 6.6 kernel, and there was no big probrem. I think the 6.12 kernel is much better.  

・If you use an SBC as a desktop PC, I think the most important matter is the ***storage***, much more important than this driver.  
&emsp; I think a microSD is too slow, so I am using a USB3.0 SSD for main storage. I don't know whether an eMMC is enough or not.

・Test results  
&emsp; All tested only with an odroid-n2-plus board and Debian.  
&emsp; So not sure whether this driver works with other board or other distribution.
| kernel | distribution | gdm3/gnome(WL) | gdm3/mate | gdm3/kde(x11)| lightdm/kde(x11) | 
| --- | --- |:---:|:---:|:---:|:---:|
| 5.15.181 | Debian10 | ○ | ○ | ○ | - | 
| 5.15.181 | Debian11 | ○ | ○ | ○ | ○ | 
| 5.15.181 | Debian12 | ○ | ○ | ○ | ○ |
| 6.6.89 | Debian10 | ○ | ○ | ○ | - | 
| 6.6.89 | Debian11 | ○ | ○ | ○ | ○ | 
| 6.6.89 | Debian12 | ○ | ○ | ○ | ○ | 
| 6.12.26 | Debian11 | - | - | - | ○ | 
| 6.12.26 | Debian12 | ○ | ○ | ○ | ○ | 

&emsp; ○: worked, ☓: not worked, -: not tested  
&emsp; Notice: I usually use KDE(x11) and ligthdm, so I have tested other environment for only several minutes,  
&emsp; &emsp; &emsp; with browsing Web and playing Youtube with Firefox.  

### How to use  
・merge this driver with mainline linux kernel source  
・make menuconfig >> select 'NOSEM DRM Driver (experimental)'  
・build and install the kernel  

### TODO  
・Optimize VD1 update  
・Comfort the spirits of 'the ghost' (fix shutdown process)  
