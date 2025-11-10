# ECU Operation

## General Operation

The following diagram shows the operational sequencing of the engine as it starts and moves through its first rotation and a half:

![image](OperationalSequencing.jpg)

The '#1 cycle' in the diagram refers to the front cylinder operation, and '#2 cycle' refers to the rear.

### Injectors
On the diagram, the '#10 injection' refers to the front cylinder injector #1, and '#20 injection' refers to the rear cylinder injector.
If you look at the ECU PCB silkscreen, these are actually numbered #11 and #21, respectively.
In addition, the ECU circuit board is designed to drive *two* injectors per cylinder.
The secondart injectors are labled #12 and #22 on the PCB.
The pairs of injectors are wired together on the PCB so they would always fire simultaneously.
The ECU software would never be able to control the injector pair independently.
It doesn't really matter though because:

* the Aprilia circuit board does not contain the parts needed to drive the secondary injectors #12/#22, and
* the throttle body itself only implements one injector for the front cylinder and another for the rear

### Crank Reference (CRn) ID Numbers

The number 'N (NNUM)' in the diagram is what the umod4 firmware refers to as 'CRn', or 'Crank Reference N', where N ranges from 0 to 11.
There are 12 CR periods because:

* the Aprilia engine tracks each rotation of the crank as 6 equal parts of 60 degrees (360 degrees being exactly 1 rotation)
* a 4-stroke engine requires 2 full rotations to perform a complete 4-stroke cycle

The diagram makes it clear how each 60 degree portion of a crank rotation maps onto the 4-stroke operation for each cylinder.

Because of the way that the crankshaft is designed and because the cylinders are splayed 60 degrees apart, the starts of power strokes for the two cylinders are spaced unequally around a rotation of the crank.
Specifically, the diagram makes it clear that a power stroke on the front cylinder starts at the beginning of CR5.
As the engine continues to rotate, the power stroke on the rear will start only five 60-degree CR intervals later at CR10.
The next power stroke on CR5 will not occur until seven more 60-degree intervals elapse until CR4.
This is one of the fundamental reasons that V-twin engines vibrate: uneven firing pulses.

### Observations

Examining the logs from a umod4 run tell us some important things.
First off, an internal combustion engine will speed up and slow down during every rotation.
It might seem obvious that the slowest part of the rotation would be during CR4 and CR9 where the engine is at the end of the compression stroke.
This is almost true, but not quite.
In fact, the slowest part of the engine rotation occurs immediately afterwards, during CR5 and CR10.
This is the initial part of the power stroke.
The thing to consider is that the while the pressure in the cylinder is increasing rapidly during CR5 and CR10, the piston is trying to push down on a connecting rod that is still nearly vertical.
As a result, the high combustion pressure is not able to translate that high pressure into turning the crankshaft.
During CR6 and CR11, the crankshaft has rotated enough that the rod angle allows the piston to accellerate the rotary motion of the crankshaft with maximum efficiency.
That's a long way of saying that if we analyze the time period that each CR period takes, CR5 should be significantly shorter than CR4, and CR11 shorter than CR10.