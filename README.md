**ESB-BLE-Timeslot**

Overview
********
This example shows how to run the BLE and ESB protocols concurrently using the MPSL interface (previously known as the timeslot interface). 
The example runs a basic Bluetooth peripheral based on the peripheral_lbs sample, and uses MPSL to request timeslots when the Bluetooth stack is idle during which ESB communication can be scheduled. 
The example can run ESB in either PTX or PRX mode, and is compatible with the standard ESB PTX/PRX samples.  

Requirements
************

- nRF Connect SDK v2.3.0
- nRF52 series development kit