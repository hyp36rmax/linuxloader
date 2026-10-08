DEV 5 — AER-02H native steering activation research build

Target: OutRun 2 SP SDX Rev A, DVP-0015A
Jennifer SHA-256: f16fc04d836a2bd8e401d8f987d4fe694fa16e18a9f870da6623d7f884911075

INSTALL AND LAUNCH
1. Extract the contents of this package into the verified Jennifer game directory
   (the folder containing the original executable named Jennifer).
2. Run Run-DEV5-Native-FFB.cmd.
3. Play a normal race, return to the menu, and close the game normally.
4. Keep the timestamped folder under AER-DEV5-captures.

The launcher configures the virtual board and diagnostics automatically.
Repeated launches preserve existing captures and create a new unique session.
An earlier failed or interrupted attempt does not require deleting a guard file
or re-extracting the package. Preflight errors remain visible in the CMD window.

The virtual SERIAL0 transport uses Jennifer's runtime-verified two-slot packet
contract for the SDX cabinet's separate left and right steering motor-driver
assemblies. Both logical slots share the one original SERIAL0 transaction.

Never remove existing game files or captures to troubleshoot the launcher.
DEV5_SUCCESS.txt confirms game-side activation evidence only; it does not
establish authentic physical Sega arcade force-feedback behavior. The research
mode does not command physical steering motors or host FFB output.
