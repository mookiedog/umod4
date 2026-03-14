# BSON Document Storage

The EP partitions part of its flash space to hold BSON (Binary JSON) documents.
These BSON documents describe things like:

* **Profiles**

  A description of how to construct EPROM images for the ECU to run.
  The "profiles" document contains a BSON array of embedded "profile" documents,
  each describing a singular profile, which defines a specific way to construct an EPROM image to be run.

* **EPROMs**

   A description of a specific EPROM image.
   In the existing system, there ccan be a number of top-level EPROM documents.
   Conceivably, these separate top-levle "eprom" documents could get wrapped up into a
   single top-level "eproms" document, much like the profiles.
   That may happen in the future, but for now, the system is designed around multiple top-level EPROMs.

At the top-most level, BSON documents are defined by 4 bytes of an overall length, all the data bytes in the doc, then a trailing '0x00' terminator byte.
It is completely fine to store BSON docs back-to-back.
The length field at the start tells a BSON parser how to skip to the next document.
The BSON partition on the EP contains all the top-level BSON docs.
On this system, the end of the BSON documents is marked by 4 bytes of 0xFF, corresponding to erased, unprogrammed Flash memory contents.
