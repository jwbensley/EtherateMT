# EtherateMT

[![Build Status](https://travis-ci.org/jwbensley/EtherateMT.svg?branch=master)](https://travis-ci.org/jwbensley/EtherateMT)
[![PayPal Donate](https://img.shields.io/badge/paypal-donate-green.svg) < buy me a pizza!](https://www.paypal.com/cgi-bin/webscr?cmd=_donations&business=james%40bensley%2eme&lc=GB&item_name=EtherateMT&currency_code=GBP)  


#### What is it


Etherate is a Linux CLI application for testing layer 2 Ethernet and MPLS  
connectivity. It can generate various Ethernet and MPLS frames for testing  
different devices such as switches/routers/firewalls etc, to test  
traffic parsing/matching/filtering/forwarding.  

Etherate is not an effective load tester, it is not designed for high  
performance. It is designed for testing many traffic parsing scenarios.  

EtherateMT is a multi-threaded ("MT") load generator (not "traffic" generator)  
and load sinker. EtherateMT simply sends random "junk" frames (or the user may  
load a custom frame from file) as fast as it can. The user can run the  
application in transmit or received mode, choose the number of worker threads  
and which method for transmission/reception to use within the Kernel (e.g.  
`sendto()` or `sendmsg()` and `PACKET_MMAP` etc.).  

The code is still in beta so it's a bit buggy but mostly works.


### FAQ and Troubleshooting

See the Wiki page: https://github.com/jwbensley/EtherateMT/wiki/FAQ-&-Troubleshooting
