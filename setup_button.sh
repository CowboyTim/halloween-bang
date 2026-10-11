#!/bin/bash
T=${T:-/dev/ttyUSB0}
def=0017
RFID=${RFID:-$def}
DVID=${DVID:-$def}
RFC=${RFC:-017}
stty -F $T 9600 cs8 -cstopb -parenb raw -echo
cat $T &
echo "BAUD=9600  (4)"
echo -ne "AT+BAUD=4\r\n"    >> $T 
sleep 1
echo "RFID=$RFID"
echo -ne "AT+RFID$RFID\r\n" >> $T 
sleep 1
echo "DVID=$DVID"
echo -ne "AT+DVID$DVID\r\n" >> $T 
sleep 1
echo "RFC=$RFC"
echo -ne "AT+RFC$RFC\r\n"   >> $T 
sleep 1
echo "POWER=-12db  (9)"
echo -ne "AT+POWE9\r\n"     >> $T 
sleep 1
echo "CLSS=C1  (button relay, default down -> UP)"
echo -ne "AT+CLSSC1\r\n"    >> $T 
sleep 1
echo -ne "AT+CLSS\r\n"      >> $T 
sleep 1
kill %1
wait
