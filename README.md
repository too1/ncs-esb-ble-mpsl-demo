**ESB-BLE-Timeslot**

Overview
********
This example shows how to run the BLE and ESB protocols concurrently using the MPSL interface (previously known as the timeslot interface). 

The example runs a basic Bluetooth peripheral based on the peripheral_lbs sample, and uses MPSL to request timeslots when the Bluetooth stack is idle during which ESB communication can be scheduled. 

The example can run ESB in either PTX or PRX mode, and is compatible with the standard ESB PTX/PRX samples.  

The example implements a simplified application level interface to the ESB protocol called 'app_esb'. 
The idea of this interface is to hide a lot of the boiler plate ESB configuration from the application itself, and set up a framework where a more application specific interface can be implemented. The drawback of this approach is that not all the ESB functions are exposed, and for all but very simple applications it will be necessary to modify the app_esb layer to add more functionality as needed. 
Using this layer on top of ESB is also allowing application code to be seamlessly ported between the nRF52 series, where everything is running in a single core, and the nRF5340, where the Bluetooth controller and the ESB protocol runs on the network core while the application runs in the application core. 

On the nRF5340 the app_esb layer is split in three parts. On the application core the app_esb_53_app.c file sets up the app_esb API and translates app_esb function calls to RPC calls using the NRF_RPC_IPC module, allowing communication between the application and network cores. The app_esb_53_net.c file receives these commands on the network core, and forwards them to the app_esb.c module. The app_esb.c implementation is shared between nRF52 and nRF53 projects, to avoid diverging implementations between the two platforms. 

For an overview of the app_esb API check app_esb.h. When making changes to the API it is necessary to modify all the app_esb source files accordingly, unless nRF53 support is not required.  

The timeslot functionality required to run ESB and BLE concurrently is handled by timeslot_handler.c, and this file will suspend and resume the app_esb.c module continuously when a timeslot is started or stopped. The timeslot handler will request timeslots continuously and try to extend the running timeslot in order to get as much radio time as possible. 
The length of the requested timeslot is set by the TIMESLOT_LENGTH_US define in timeslot_handler.c, and depending on the Bluetooth advertising and connection parameters it might be necessary to change this (if the connection interval is too short the default timeslot length of 10ms might be too much). 

The Bluetooth setup is handled by the app_bt_lbs.c module. Currently the only interface between the application and this module is the init function, but more functions can be added as needed. 

Requirements
************

- nRF Connect SDK v2.5.0
- One of the following development kits:
    - nRF52DK
    - nRF52833DK
    - nRF52840DK
    - nRF5340DK

TODO
****

- Reliability testing, on the nRF5340 in particular
- More functionality in app_esb, primarily to allow changing ESB configuration and enabled/disabled status at runtime
- Add basic send/receive functions to the app_bt_lbs module
- General code cleanup