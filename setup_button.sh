#!/bin/bash
T=${T:-/dev/ttyUSB0}
stty -F $T 9600 cs8 -cstopb -parenb raw -echo
cat $T &
echo -ne "AT+BAUD=4\r\n"    >> $T 
sleep 1
echo -ne "AT+RFID$RFID\r\n" >> $T 
sleep 1
echo -ne "AT+DVID$DVID\r\n" >> $T 
sleep 1
echo -ne "AT+RFC$RFC\r\n"   >> $T 
sleep 1
echo -ne "AT+POWE9\r\n"     >> $T 
sleep 1
echo -ne "AT+CLSSC1\r\n"    >> $T 
sleep 1
echo -ne "AT+CLSS\r\n"      >> $T 
sleep 1
kill %1
wait
