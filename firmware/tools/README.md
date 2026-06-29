UPS-SNMP tools suite
---

# Description

Small Python scripts to performs "automated" actions on UPS-SNMP devices.
The main usage is to batch flash a new firmware version.

# Installation

Install a recent version of Python (>3.6)

Create a new Python venv
``` bash
> python -m venv /path/to/new/virtual/environment
```

Activate it:

Linux:
```bash
> source /path/to/new/virtual/environment/bin/activate
```

Windows (Powershell):
```powershell
> /path/to/new/virtual/environment\Scripts\Activate.ps1
```

Install dependencies (from this folder):
``` bash
> pip install -e .
```

# Usage
``` bash
> python -m ups-snmp-tools
usage: ups-snmp tool [-h] [--timeout TIMEOUT] [--ipColumn IPCOLUMN] [--nameColumn NAMECOLUMN]
                     [--nameSubstitution NAMESUBSTITUTION] [--sheetName SHEETNAME] [--fixName]
                     list update
```

Two parameters are mandatory:
- list: Specify the Excel file listing devices IP and (optional) name
- update: Specify the path to the firmware update file (.fw)

Other parameters:
- timeout: Specify the HTTP timeout (seconds) to give up, default is 5s
- ipColumn: Specify the column in the Excel file containing the device IP address (starting from 1, defaut 2)
- nameColumn: Specify the column in the Excel file containing the device name (starting from 1). This parameter is optional.
- nameSubstitution: Specify a substitution to apply to device name syntax is `SEARCH/REPLACE` where SEARCH is the searched sequence and REPLACE is the replaced sequence.
- sheetName: Name of the Excel sheet to find data in the file.
- fixName: If set, the application will update the device name according to provided name and substitution.

Example:
```bash
python -m ups-snmp-tools devices.xlsx waveshare_esp32_eth_ch1115_v0.0.5.fw --nameColumn 1 --nameSubstitution "sw-/ups-"
```