# Comparing EPROM Maps

The entire set of RP58-compatible restricted and derestricted tables can be dumped as shown below.
The dump command leaves out the address info because it would obviously be different between the two table sets.

The idea is to dump the tables as hex characters so that a diff command can be used to tell if there are differences.

The restricted tables live in the EPROM from offset 0x00A2..0x0855 (length is 0x7B4).
They can be dumped into a text form via:

    od -v -t x1 -A none -j 0xA2 -N 0x7b4 RP58.bin>RP58.bin.restricted

The derestricted tables are at offset 0x8856..0x8A25 (length 0x7B4).
They can be dumped in similar fashion:

    od -v -t x1 -A none -j 0x856 -N 0x7b4 RP58.bin>RP58.bin.derestricted

After dumping, the text files can be compared with a standard 'diff' command.
Doing this shows that a 549USA EPROM has no differences between the table sets, as expected.
In contrast, a 'diff' operations will show that the restricted and derestricted tables differ in a European RP58 EPROM.

For simplicity, a bashscript 'testmaps' is included in the src directory.
Using the RP58 bin file as an example, the script can be run as:

    ./testmaps RP58.bin

The script will generate text files containing the restricted and derestricted tables, then run 'diff' on the generated tables.
If the result of running the script produces no output, the restricted and derestricted tables are identical.
If the script produces a wad of text output, then the restricted and derestricted tables are different.

Checking each EPROM image that I have been able to find leads to the following table:

| EPROM | Derestrictable |
|---|---|
| 549USA | **No** |
| RP58 | Yes |
| RP58USA | **No** |
| 549EuroC | Yes |
| 549EuroKatD | Yes |
| 8796505 | Yes |
| 8796529 | Yes |
| 8796539 | Yes |
| Edwards | **No** |
| PA59 | Yes |

For EPROMs that are derestrictable, I do not know what the specific differences are between the maps, only that they are not the same.

## Derestriction

The RP58 represents the most modern codebase for the Gen 1 ECU.
EPROMs built on the RP58 codebase contain a complete pair of injection maps, known as the "restricted" maps and the "derestricted" maps.
The default "restricted" maps were designed for street use.
They sought to balance engine performance with meeting emissions regulations.
The secondary set of "derestricted" maps were designed for track use only.
The derestriction process involved pulling baffles from the exhaust and intake tracts, then cutting a specific wire in the wiring harness as it entered the ECU.
If the ECU saw the wire had been cut, it would operate using the secondary "derestricted" map set instead of using the default maps.
The derestricted track-only maps increased engine output by roughly 10 rear-wheel HP, according to magazine reviews at the time.
Reconnecting the wire would reactivate the street maps.

The ability to "derestrict" the ECU was only available until 2002 in North America.
Subsequent bikes were either shipped with RP58USA or 549USA EPROMs where cutting the wire made no difference: the ECU only ran street maps.
Interestingly, the codebase between the restricted and derestricted EPROMs is identical.
The derestriction feature was deactivated by duplicating the street maps in both the restricted and the derestricted locations.
Cutting the wire on a 549USA or RP58USA still activates the secondary maps.
But since both maps now contain identical data, it makes no difference.

Note that the street maps are identical between the RP58 and the RP58USA.
