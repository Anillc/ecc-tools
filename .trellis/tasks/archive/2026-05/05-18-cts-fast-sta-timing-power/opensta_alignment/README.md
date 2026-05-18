# OpenSTA CTS Fast STA Alignment

This directory contains a deterministic micro-case used to compare the iCTS CTS
fast STA model against OpenSTA `dmp_ceff_elmore`.

Run from any directory:

```bash
/home/liweiguo/project/OpenROAD/build-opensta-codex/sta -no_init -exit cts_fast_sta_alignment.tcl
```

The case intentionally uses:

- one tiny Liberty buffer with NLDM delay/slew and internal-power tables,
- a two-buffer clock-like path,
- manually annotated Pi/Elmore parasitics through OpenSTA Tcl,
- `report_dcalc` for cell delay, driver slew, Ceff, and load timing,
- `report_checks` for propagated arrival/slew/cap,
- `report_power -format json` for CTS-relevant power terms.
