import os
import pyexcel
import argparse
import re
import pprint
import requests
import struct

espList = []

def getFWVersion(fwFile):
    pattern = re.search(r".*_(.*).fw", fwFile)
    if pattern is not None:
        version = pattern.group(1)
        if version is not None:
            print(f'File version : {pattern.group(1)}')
            return pattern.group(1)
    return None

def checkESP(ip):
    response = requests.get(f'http://{ip}/api/status')
    response = response.json()
    return response["System"]["Version"]

def openFile(file, ipCol, nameCol, sheetName):
    book = pyexcel.get_book(file_name=file)
    spreadsheet = book[sheetName]
    pattern = re.compile(r"(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)")
    for r in spreadsheet.row_range():
        ip = spreadsheet.cell_value(r, ipCol-1)
        if not pattern.match(ip):
            continue
        name = spreadsheet.cell_value(r, nameCol-1)
        device = {"ip" : ip, "name" : name}
        espList.append(device)
    return len(espList)

def updateESP(file, ip):
    url = f'http://{ip}/api/ota'
    with open(file, 'rb') as f:
        r = requests.post(url, data=f)
    if r.status_code >= 200 and r.status_code < 400:
        print(f'ESP {ip} updated!')
    else:
        print(f'Unable to update ESP {ip}.')

def main():
    parser = argparse.ArgumentParser(description='Batch flash.')
    parser.add_argument('--timeout', type=int, default=5, help='HTTP timeout in seconds.')
    parser.add_argument('--ipColumn', type=int, default=2, help='IP column index.')
    parser.add_argument('--nameColumn', type=int, default=1, help='Name column index.')
    parser.add_argument('--sheetName', type=str, default="VLAN2", help='Sheet name.')
    parser.add_argument('list', type=str, help='Path to ESP Excel file.')
    parser.add_argument('update', type=str, help='Path to ESP firmware file.')
    args = parser.parse_args()

    fwVersion = getFWVersion(args.update)

    openFile(args.list, args.ipColumn, args.nameColumn, args.sheetName)
    print(f'Found {len(espList)} devices.')
    for esp in espList:
        espIP = esp["ip"]
        remVersion = checkESP(espIP)
        if fwVersion != remVersion:
            updateESP(args.update, espIP)
        else:
            print(f'ESP {espIP} already running version : {remVersion}')
if __name__ == '__main__':
    main()