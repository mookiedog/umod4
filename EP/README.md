# EP: EPROM Processor

The **E**PROM **P**rocessor (**EP** for short) is an RP2040 that has two major tasks. The first task is to act like a replacement for the ECU EPROM chip. The second task is receive the ECU data stream arriving from the ECU firmware and pass it on to the Wireless Processor (WP) for logging.

The RP2040 Core 1 is used to implement the EPROM functionality. Core 0 is responsible for sending the ECU data stream to the WP and managing the Flash memory. The ECU images are stored in a special partition of the Flash. Core 0 is responsible for adding and deleting ECU flash images based on commands from the WP. Core 0 is underutilized from a CPU perspective, but that is because the EPROM interface to the ECU uses up 27 of the RP2040's 30 GPIO pins. Two of those remaining 3 GPIOs are used to implement a bidirectional communication channel between the EP's Core1 and the WP.

Because the EPROM functionality is just a software construct, we can expand on a traditional EPROM's functionality in some interesting and useful ways. For example, the EPROM code allows for unused parts of the EPROM address space to act like RAM instead of read-only EPROM. The ultraMod ECU firmware takes advantage of this by relocating the processor stack into a new RAM area deep inside the EPROM address space. This has a number of benefits.  Firstly, it ensures that the unexpected stack excursions will not corrupt the rest of ECU RAM under essentially any circumstances. If the ECU stack ever wanders outside of its proper boundaries, it will be accessing read-only memory. This will have a beneficial side effect of causing the processor to crash in short order. That might sound bad, but it beats a standard stack overflow event that silently corrupts some part of the global RAM space resulting in undefined and peculiar ECU behavior.  Finally, the increased RAM will be beneficial for people writing their own ECU software because more RAM is always a good thing.

Another feature to aid people writing their own ECU code is that the RP2040 is set up to log all HC11 accesses to the EPROM address space. All accesses to the EPROM address space (including the converted RAM space[s]) are logged to a circular buffer maintaining the last 16,384 accesses. This could be very useful when debugging HC11 code changes.


## Building Instructions

1) Use VS Code to open a remote session to the top-level umod4 directory
1) Building the umod4 project (F7) will build both the EP and WP targets
1) To debug the EP, use the 'select the target to lanch' button on the status bar and select the 'EP'
1) Start debugging by selecting the proper debug hardware from the 'run and debug' pulldown, then hitting F5
