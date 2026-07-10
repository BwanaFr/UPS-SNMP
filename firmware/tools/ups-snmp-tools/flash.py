import pyexcel
import argparse
import re
import requests
import json
import sys

espList = []
timeout = 5

def getFWVersion(fwFile):
    pattern = re.search(r".*_(.*).fw", fwFile)
    if pattern is not None:
        version = pattern.group(1)
        if version is not None:
            print(f'File version : {pattern.group(1)}')
            return pattern.group(1)
    return None

def checkESP(ip):
    response = requests.get(f'http://{ip}/api/status', timeout=timeout)
    response = response.json()
    return response["System"]["Version"]

def getName(esp):
    if esp["name"] is not None:
        return f'{esp["name"]} ({esp["ip"]})'
    else:
        return esp["ip"]

def openFile(file, ipCol, nameCol, nameFilter, ipSub, nameSub, sheetName):
    book = pyexcel.get_book(file_name=file)
    spreadsheet = book[sheetName]
    pattern = re.compile(r"(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)")
    for r in spreadsheet.row_range():
        ip = spreadsheet.cell_value(r, ipCol-1)
        if not pattern.match(ip):
            continue

        if ipSub is not None:
                subs = ipSub.split('/', 1)
                if(len(subs) == 2):
                    ip = ip.replace(subs[0], subs[1])
                else:
                    print(f'Bad IP substitution {ipSub}.')
        if nameCol is not None:
            name = spreadsheet.cell_value(r, nameCol-1)
            if nameFilter:
                filterRe = re.compile(nameFilter)
                if not filterRe.match(name):
                    continue
            if nameSub is not None:
                subs = nameSub.split('/', 1)
                if(len(subs) == 2):
                    name = name.replace(subs[0], subs[1])
                else:
                    print(f'Bad name substitution {nameSub}.')
        else:
            name = None
        device = {"ip" : ip, "name" : name}
        espList.append(device)
    return len(espList)

def fixName(esp):
    if esp["name"]:
        url = f'http://{esp["ip"]}/api/config'
        data = {"deviceName": esp["name"]}
        print(f'Updating device {esp["ip"]} name to {esp["name"]}')
        try:
            r = requests.post(url, data=json.dumps(data), timeout=timeout)
            if r.status_code != 200:
                print(f'Unable to update device name on {esp["ip"]}')
        except:
            print(f'Unable to update device name on {esp["ip"]}')

def updateESP(file, esp):
    url = f'http://{esp.ip}/api/ota'
    with open(file, 'rb') as f:
        r = requests.post(url, data=f)
    if r.status_code >= 200 and r.status_code < 400:
        print(f'ESP {getName(esp)} updated!')
    else:
        print(f'Unable to update ESP {getName(esp)}.')

def main():
    parser = argparse.ArgumentParser(prog="ups-snmp tool", description='Batch flash.')
    parser.add_argument('--timeout', type=int, default=1, help='HTTP timeout in seconds.')
    parser.add_argument('--ipColumn', type=int, default=2, help='IP column index.')
    parser.add_argument('--nameColumn', type=int, default=None, help='Name column index.')
    parser.add_argument('--nameFilter', type=str, default=None, help='Name filter regex.')
    parser.add_argument('--nameSubstitution', type=str, default=None, help="Name substitution.")
    parser.add_argument('--ipSubstitution', type=str, default=None, help="IP substitution.")
    parser.add_argument('--sheetName', type=str, default="VLAN2", help='Sheet name.')
    parser.add_argument('--fixName', action='store_true', help='Fix device name.')
    parser.add_argument('--update', type=str, default=None, help='Path to ESP firmware file.')
    parser.add_argument('list', type=str, help='Path to ESP Excel file.')
    args = parser.parse_args()

    global timeout
    timeout = args.timeout

    if args.update is not None:
        fwVersion = getFWVersion(args.update)

    openFile(args.list, args.ipColumn, args.nameColumn, args.nameFilter, args.ipSubstitution, args.nameSubstitution, args.sheetName)
    print(f'Found {len(espList)} devices.')
    for esp in espList:
        espIP = esp["ip"]
        if args.update is not None:
            remVersion = checkESP(espIP)
            if fwVersion != remVersion:
                print(f'Updating {getName(esp)} from {remVersion} to {fwVersion}')
                updateESP(args.update, esp)
            else:
                print(f'ESP {getName(esp)} already running version : {remVersion}')

        if args.fixName:
            fixName(esp)

if __name__ == '__main__':
    main()