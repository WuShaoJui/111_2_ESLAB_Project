# ble_scan_connect.py:
from bluepy.btle import Peripheral, UUID, AssignedNumbers
from bluepy.btle import Scanner, DefaultDelegate
import threading
import time
import RPi.GPIO as GPIO
import adafruit_dht
import board

dhtDevice = adafruit_dht.DHT11(board.D21, use_pulseio=False)

#GPIO.setwarnings(False)
#GPIO.setmode(GPIO.BOARD)
GPIO.cleanup()
GPIO.setup(40,GPIO.IN) #DHT
# Right
GPIO.setup(2,GPIO.OUT) #B
GPIO.setup(3,GPIO.OUT) #C
GPIO.setup(4,GPIO.OUT) #D
GPIO.setup(5,GPIO.OUT) #E
GPIO.setup(6,GPIO.OUT) #F
GPIO.setup(8,GPIO.OUT) #G
GPIO.setup(7,GPIO.OUT) #A
GPIO.setup(19,GPIO.OUT) #DOT

# Left
GPIO.setup(9,GPIO.OUT) #A
GPIO.setup(10,GPIO.OUT) #B
GPIO.setup(11,GPIO.OUT) #C
GPIO.setup(12,GPIO.OUT) #D
GPIO.setup(13,GPIO.OUT) #E
GPIO.setup(16,GPIO.OUT) #F
GPIO.setup(17,GPIO.OUT) #G
GPIO.setup(25,GPIO.OUT) #DOT

GPIO.setup(20,GPIO.OUT) #LED inst
GPIO.setup(22,GPIO.OUT) #LED Light
chars = [
   [0,0,0,0,0,0,1], #0
   [1,0,0,1,1,1,1], #1
   [0,0,1,0,0,1,0], #2
   [0,0,0,0,1,1,0], #3
   [1,0,0,1,1,0,0], #4
   [0,1,0,0,1,0,0], #5
   [0,1,0,0,0,0,0], #6
   [0,0,0,1,1,1,1], #7
   [0,0,0,0,0,0,0], #8
   [0,0,0,0,1,0,0]  #9
   ]

class ScanDelegate(DefaultDelegate):
    def __init__(self):
        DefaultDelegate.__init__(self)
    def handleDiscovery(self, dev, isNewDev, isNewData):
        if isNewDev:
            print ("Discovered device", dev.addr)
        elif isNewData:
            print ("Received new data from", dev.addr)
scanner = Scanner().withDelegate(ScanDelegate())
devices = scanner.scan(10.0)
n=0
addr = []
device = 0
for dev in devices:
    print ("%d: Device %s (%s), RSSI=%d dB" % (n, dev.addr,
    dev.addrType, dev.rssi))
    if(dev.addr == "f6:4c:0b:d8:93:d4"):
        device = str(n)
    addr.append(dev.addr)
    n += 1
    for (adtype, desc, value) in dev.getScanData():
        print (" %s = %s" % (desc, value))

number = device# input('Enter your device number: ')
print ('Device', number)
num = int(number)
print (addr[num])
#
print ("Connecting...")
dev = Peripheral(addr[num], 'random')
#
print ("Services...")
for svc in dev.services:
    print (str(svc))
#

CharHandel2Svc = {}
Data = {}

print ("Setting CCCDs...")
for svc in dev.services:
    for ch in svc.getCharacteristics():
        desc = ch.getDescriptors(0x2902)
        if(len(desc)):
            dev.writeCharacteristic(desc[0].handle, b"\x01\x00")
            Data[svc.uuid] = 0
            CharHandel2Svc[ch.getHandle()] = svc.uuid
     
def record_time(cHandle, data):
    global current_clap_time
    current_clap_time = data[-4] + data[-3]*16**2 + data[-2]*16**4 + data[-1]*16**6

dev.delegate.handleNotification = record_time

# init

ev_notification = threading.Event() 

inst = ""

last_clap_time = 0
current_clap_time = 0

light_flag = False
GPIO.output(22, light_flag)
time_flag = 0

temper = 0
humid = 0

count_down_enable = False
count_down_remain = 0

seven_set_switch = False

def update_DHT():
    global temper, humid, dhtDevice
    while True:
        try:
            # Print the values to the serial port
            temperature_c = dhtDevice.temperature
            if(str(type(temperature_c)) == "<class 'int'>"):
                temper = int(temperature_c)
            humidity = dhtDevice.humidity
            if(str(type(humidity)) == "<class 'int'>"):
                humid = int(humidity)

        except RuntimeError as error:
            # Errors happen fairly often, DHT's are hard to read, just keep going
            # print(error.args[0])
            time.sleep(1.0)
            continue
        except Exception as error:
            dhtDevice.exit()
            raise error

        time.sleep(1.0)

def clear_inst():
    global inst, last_clap_time, current_clap_time
    if(last_clap_time != 0):
        print ("Clear inst!!")
        inst = ""
        last_clap_time = 0
        current_clap_time = 0
        GPIO.output(20, False)

def seven_reset():
    global time_flag, count_down_enable
    time_flag = 0
    count_down_enable = False

def display(left, right, dot_l,dot_r):
    global chars
    GPIO.output(9, bool(chars[left][0])) 
    GPIO.output(10, bool(chars[left][1]))
    GPIO.output(11, bool(chars[left][2]))  
    GPIO.output(12, bool(chars[left][3])) 
    GPIO.output(13, bool(chars[left][4])) 
    GPIO.output(16, bool(chars[left][5])) 
    GPIO.output(17, bool(chars[left][6]))
    GPIO.output(25, bool(dot_l))

    GPIO.output(7, bool(chars[right][0])) 
    GPIO.output(2, bool(chars[right][1]))
    GPIO.output(3, bool(chars[right][2]))  
    GPIO.output(4, bool(chars[right][3])) 
    GPIO.output(5, bool(chars[right][4])) 
    GPIO.output(6, bool(chars[right][5])) 
    GPIO.output(8, bool(chars[right][6])) 
    GPIO.output(19, bool(dot_r))


def display_time():
    global chars, sem_seven, time_flag
    switch = 0
    while True:
        if time_flag == 0:
            time.sleep(1)
            continue
        t = time.time()
        time_tuple = time.ctime(t)
        hour = int(time_tuple[11:13])
        minute = int(time_tuple[14:16])
        if(switch % 2 == 0):
            display(hour//10, hour%10,1, 0)
        else:
            display(minute//10, minute%10,1, 1)
        switch += 1
        time.sleep(1)

def count_down():
    global count_down_remain, count_down_enable, seven_set_switch
    while True:
        if(count_down_enable):
            ten = int(count_down_remain//10)
            ten = max(ten, 0)
            ten = min(ten, 9)
            one = int(count_down_remain%10)
            one = max(one, 0)
            one = min(one, 9)
            if(count_down_remain >= 10):
                display(ten, one, 1, 1)
            elif(count_down_remain != 0):
                display(one, int(count_down_remain*10%10), 0, 1)
            elif(seven_set_switch): # left
                display(0, 0, 0, 1)
            else:
                display(0, 0, 1, 0)
            time.sleep(0.1)
            if(count_down_remain > 0.05):
                count_down_remain -= 0.1
            if(count_down_remain >= 10):
                display(ten, one, 1, 1)
            elif(count_down_remain != 0):
                display(one, int(count_down_remain*10%10), 0, 1)
            elif(seven_set_switch): # left
                display(0, 0, 0, 1)
            else:
                display(0, 0, 1, 0)
            if(count_down_remain < 0.05):
                count_down_remain = int(0)
                count_down_enable = False
        else: time.sleep(1)
            
def f0():
    global inst, count_down_remain
    print("f0 executed\n")
    clear_inst()
    seven_reset()
    if(seven_set_switch): # left
        count_down_remain = count_down_remain%10 + 10 * 0
        display(count_down_remain//10, count_down_remain%10, 0, 1)
    else:
        count_down_remain = count_down_remain//10 * 10 + 0
        display(count_down_remain//10, count_down_remain%10, 1, 0)

def f1():
    global inst, count_down_remain
    print("f1 executed\n")
    clear_inst()
    seven_reset()
    if(seven_set_switch): # left
        count_down_remain = count_down_remain%10 + 10 * 1
        display(count_down_remain//10, count_down_remain%10, 0, 1)
    else:
        count_down_remain = count_down_remain//10 * 10 + 1
        display(count_down_remain//10, count_down_remain%10, 1, 0)


def f2():
    global inst, count_down_remain 
    print("f2 executed\n")
    clear_inst()
    seven_reset()
    if(seven_set_switch): # left
        count_down_remain = count_down_remain%10 + 10 * 2
        display(count_down_remain//10, count_down_remain%10, 0, 1)
    else:
        count_down_remain = count_down_remain//10 * 10 + 2
        display(count_down_remain//10, count_down_remain%10, 1, 0)


def f3():
    global inst, count_down_remain 
    print("f3 executed\n")
    clear_inst()
    seven_reset()
    if(seven_set_switch): # left
        count_down_remain = count_down_remain%10 + 10 * 3
        display(count_down_remain//10, count_down_remain%10, 0, 1)
    else:
        count_down_remain = count_down_remain//10 * 10 + 3
        display(count_down_remain//10, count_down_remain%10, 1, 0)
def f4():
    global inst, count_down_remain 
    print("f4 executed\n")
    clear_inst()
    seven_reset()
    if(seven_set_switch): # left
        count_down_remain = count_down_remain%10 + 10 * 4
        display(count_down_remain//10, count_down_remain%10, 0, 1)
    else:
        count_down_remain = count_down_remain//10 * 10 + 4
        display(count_down_remain//10, count_down_remain%10, 1, 0)

def f5():
    global inst, count_down_remain 
    print("f5 executed\n")
    clear_inst()
    seven_reset()
    if(seven_set_switch): # left
        count_down_remain = count_down_remain%10 + 10 * 5
        display(count_down_remain//10, count_down_remain%10, 0, 1)
    else:
        count_down_remain = count_down_remain//10 * 10 + 5
        display(count_down_remain//10, count_down_remain%10, 1, 0)

def f6():
    global inst, count_down_remain 
    print("f6 executed\n")
    clear_inst()
    seven_reset()
    if(seven_set_switch): # left
        count_down_remain = count_down_remain%10 + 10 * 6
        display(count_down_remain//10, count_down_remain%10, 0, 1)
    else:
        count_down_remain = count_down_remain//10 * 10 + 6
        display(count_down_remain//10, count_down_remain%10, 1, 0)

def f7():
    global inst, count_down_remain 
    print("f7 executed\n")
    clear_inst()
    seven_reset()
    if(seven_set_switch): # left
        count_down_remain = count_down_remain%10 + 10 * 7
        display(count_down_remain//10, count_down_remain%10, 0, 1)
    else:
        count_down_remain = count_down_remain//10 * 10 + 7
        display(count_down_remain//10, count_down_remain%10, 1, 0)

def f8():
    global inst, count_down_remain 
    print("f8 executed\n")
    clear_inst()
    seven_reset()
    if(seven_set_switch): # left
        count_down_remain = count_down_remain%10 + 10 * 8
        display(count_down_remain//10, count_down_remain%10, 0, 1)
    else:
        count_down_remain = count_down_remain//10 * 10 + 8
        display(count_down_remain//10, count_down_remain%10, 1, 0)

def f9():
    global inst, count_down_remain 
    print("f9 executed\n")
    clear_inst()
    seven_reset()
    if(seven_set_switch): # left
        count_down_remain = count_down_remain%10 + 10 * 9
        display(count_down_remain//10, count_down_remain%10, 0, 1)
    else:
        count_down_remain = count_down_remain//10 * 10 + 9
        display(count_down_remain//10, count_down_remain%10, 1, 0)

def f10():
    global inst, seven_set_switch
    print("f10 executed\n")
    clear_inst()
    seven_reset()
    seven_set_switch = not seven_set_switch
    if(seven_set_switch):
        display(count_down_remain//10, count_down_remain%10, 0, 1)
    else:
        display(count_down_remain//10, count_down_remain%10, 1, 0)

def f11():
    global inst, count_down_enable
    print("f11 executed\n")
    clear_inst()
    seven_reset()
    count_down_enable = True

def f12():
    global inst, light_flag
    print("f12 executed\n")
    clear_inst()
    light_flag = not light_flag
    GPIO.output(22, light_flag)

def f13():
    global inst 
    print("f13 executed\n")
    clear_inst()
    seven_reset()
    display(int(humid)//10, int(humid)%10, 1, 1)

def f14():
    global inst 
    print("f14 executed\n")
    clear_inst()
    seven_reset()
    display(int(temper)//10, int(temper)%10, 1, 1)

def f15():
    global inst, time_flag
    print("f15 executed\n")
    clear_inst()
    seven_reset()
    time_flag = 1
        

inst_set = {"0000": f0,  "0001": f1,  "0010": f2,  "0011": f3,\
            "0100": f4,  "0101": f5,  "0110": f6,  "0111": f7,\
            "1000": f8,  "1001": f9,  "1010": f10, "1011": f11,\
            "1100": f12, "1101": f13, "1110": f14, "1111": f15}

def try_inst():
    global inst, inst_set
    if(len(inst) == 4):
        print("Instruction start executing!!")
        inst_set.get(inst)()
    else:
        print("Instruction lenth != 4!! Lenth = ", len(inst), "\n")

def walk():
    global inst, last_clap_time, current_clap_time, clap_flag

    clap_flag = 1

    if(last_clap_time == 0):
        last_clap_time = current_clap_time
        return
    
    if((current_clap_time - last_clap_time) / 500.0 > 0.7):
        inst += "1"
    elif((current_clap_time - last_clap_time) / 500.0 > 0.1):
        inst += "0"
    else:
        return
    last_clap_time = current_clap_time   

def BLE():
    global current_clap_time, last_clap_time, ev_notification
    try:
        print ("Waiting for notifications...")
        while True:
            time.sleep(0.001)
            if not dev.waitForNotifications(3):
                clear_inst()
                continue
            print("clap time: ", current_clap_time / 500.0)
            print("")
            # notify
            ev_notification.set()
    finally:
        dev.disconnect()

def PERI():
    global ev_notification
    while True:
        ev_notification.wait()
        ev_notification.clear()
        GPIO.output(20, True)
        # walk on the decision tree
        walk()
        # check the inst
        try_inst()
        time.sleep(0.001)


T_BLE = threading.Thread(target = BLE)
T_PERI = threading.Thread(target = PERI)
T_DHT = threading.Thread(target = update_DHT)
T_TIME = threading.Thread(target = display_time)
T_COUNT = threading.Thread(target = count_down)
T_BLE.start()
T_PERI.start()
T_DHT.start()
T_TIME.start()
T_COUNT.start()


