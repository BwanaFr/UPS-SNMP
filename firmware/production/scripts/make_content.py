from datetime import datetime, date, time, timezone
import locale
import gzip
from io import BytesIO
import binascii
import shutil
import os
import mimetypes
import hashlib
import re

len_suffix = 'len'
mimetype_suffix = 'mimetype'
etag_suffix = 'etag'
data_suffix = 'data'
last_modified_suffix = 'last_modified'
out_source = './src/StaticWebContent.cpp'

locale.setlocale(locale.LC_TIME, "C")
dt = datetime.now()
lm = dt.strftime("%a, %d %b %Y %H:%M:%S GMT")
totalBytes = 0
updatedFiles = 0

def find_file(in_f, filename):
    if in_f:
        in_f.seek(0)
        print(f'Looking for {filename} in cpp')
        fileLen = None
        fileMimeType = None
        fileETag = None
        fileData = None
        fileLastModified = None
        f_n = filename.replace('.', "_").replace('-', '_')        
        regx = r".*static const static_file_t " + f_n + r'.*=.*'
        endOfStruct = r'.*};.*\n'
        structData = r'\s*\.(.*)\s=\s*\"?(.*?)\"?,\s*\n'
        structFound = False
        fileData = r'\s*const uint8_t ' + f_n + '_' + data_suffix + '\[\]\s*=\s*(.*);\n'
        for line in in_f:
            if not structFound:
                res = re.match(regx, line)
                if res:
                    structFound = True
                res = re.match(fileData, line)
                if res:
                    fileData = res.group(1)
            else:
                if re.match(endOfStruct, line):
                    structFound = False
                else:
                    res = re.match(structData, line)
                    if res:
                        if res.group(1) == len_suffix:
                            fileLen = res.group(2)
                        elif res.group(1) == mimetype_suffix:
                            fileMimeType = res.group(2)
                        elif res.group(1) == etag_suffix:
                            fileETag = res.group(2)
                        elif res.group(1) == last_modified_suffix:
                            fileLastModified = res.group(2)
                
                if fileLen and fileMimeType and fileETag and fileData and fileLastModified:
                    ret = {
                        "name" : filename,
                        len_suffix : fileLen,
                        mimetype_suffix : fileMimeType,
                        etag_suffix : fileETag,
                        data_suffix : fileData,
                        last_modified_suffix : fileLastModified
                    }
                    return ret
    return None

def createFile(absPath, etag):
    filename = os.path.basename(absPath)
    print('Compressing file ', filename)

    buf = BytesIO()
    with open(absPath, 'rb') as f_in:
        with gzip.GzipFile(mode='wb', fileobj=buf) as f_out:
            shutil.copyfileobj(f_in, f_out)
    compressed = buf.getvalue()    
    print('Compressed Data:', len(compressed), ' bytes')
    
    mimetype = mimetypes.guess_type(filename)[0]
    print('mimetype : ', mimetype)
    
    byteArray = '{'
    for b in compressed:
        byteArray += "0x{0:x},".format(b)
    byteArray += '}'

    ret = {
        "name" : filename,
        len_suffix : len(compressed),
        mimetype_suffix : mimetype,
        etag_suffix : etag,
        data_suffix : byteArray,
        last_modified_suffix : lm        
    }
    return ret

#Start
print('Generating static web content')
files = []


in_f = None
if os.path.isfile(out_source):
    in_f = open('./src/StaticWebContent.cpp', 'r')

for filename in os.listdir('./html'):
    absPath = './html/' + filename
    if os.path.isfile(absPath):
        #Compute sha1 of the unzipped file
        with open(absPath, 'rb', buffering=0) as f:
            etag = hashlib.file_digest(f, 'sha1').hexdigest()
        #Find the file in the current source file
        foundFile = find_file(in_f, filename)        
        if foundFile:
            #Found, compare sha1
            if foundFile[etag_suffix] != etag :
                print(f'File {filename} Etag mismatch {foundFile[etag_suffix]} vs {etag}')
                foundFile = None
        if not foundFile:
            print(f'Building file {absPath}')
            updatedFiles += 1
            foundFile = createFile(absPath, etag)
        files.append(foundFile)

print(f'Found {len(files)} file(s), updated {updatedFiles}')
with open('./src/StaticWebContent.cpp', 'w') as out_f:
    out_f.write('//Automatically generated with make_content script, do not edit!\n\n')
    out_f.write('#include "StaticWebContent.h"\n')
    out_f.write('#include <Arduino.h>\n')
    out_f.write('#include <string>\n')
    # out_f.write('#include "esp_log.h"\n\n')
    out_f.write('static const char *TAG = "WebStatic";\n\n')

    for f in files:
        totalBytes += int(f[len_suffix])
        f_n = f["name"].replace('.', "_").replace('-', '_')
        # out_f.write(f'static const size_t {f_n}_{len_suffix} = {f[len_suffix]};\n')
        # out_f.write(f'static const char* {f_n}_{mimetype_suffix} = "{f[mimetype_suffix]}";\n')
        # out_f.write(f'static const char* {f_n}_{etag_suffix} = "{f[etag_suffix]}";\n')
        out_f.write(f'const uint8_t {f_n}_{data_suffix}[] = {f[data_suffix]};\n')
        # out_f.write(f'static const char* {f_n}_{last_modified_suffix} = "{f[last_modified_suffix]}";\n\n')
        out_f.write(f'static const static_file_t {f_n} = {{\n')
        out_f.write(f'\t.file_name = "{f["name"]}",\n')
        out_f.write(f'\t.len = {f[len_suffix]},\n')
        out_f.write(f'\t.mimetype = "{f[mimetype_suffix]}",\n')
        out_f.write(f'\t.etag = "{f[etag_suffix]}",\n')
        out_f.write(f'\t.data = {f_n}_{data_suffix},\n')
        out_f.write(f'\t.last_modified = "{f[last_modified_suffix]}",\n')
        out_f.write('};\n\n')

    out_f.write('static const static_file_t* www_static_files[] = {\n')
    for f in files:
        f_n = f["name"].replace('.', "_").replace('-', '_')
        out_f.write(f'\t\t&{f_n},\n')
    out_f.write('\t\tnullptr};\n\n')

    out_f.write('static bool get_req_header(const char* headerName, std::string& value, httpd_req_t *req){\n')
    out_f.write('\tsize_t buf_len = 0;\n')
    out_f.write('\tbuf_len = httpd_req_get_hdr_value_len(req, headerName);\n')
    out_f.write('\tif(buf_len > 1){\n')
    out_f.write('\t\tvalue.resize(buf_len+1);\n')
    out_f.write('\t\tesp_err_t ret = httpd_req_get_hdr_value_str(req, headerName, &value[0], buf_len+1);\n')
    out_f.write('\t\tif (ret == ESP_OK) {\n')
    out_f.write('\t\t\tvalue.resize(buf_len);	//Remove the \\0\n')
    # out_f.write('\t\t\tESP_LOGI(TAG, "Found header => %s: |%s|", headerName, value.c_str());\n')
    out_f.write('\t\t\treturn true;\n')
    out_f.write('\t\t}\n')
    out_f.write('\t}\n')
    out_f.write('\treturn false;\n')
    out_f.write('}\n\n')

    out_f.write('static const static_file_t* getFileInfo(const char* uri)\n{\n')
    out_f.write('\tconst char* fileName = uri;\n')
    out_f.write('\tif(uri[0] == \'/\'){\n')
    out_f.write('\t\tfileName = uri +1;\n')
    out_f.write('\t}\n')
    out_f.write('\tfor(int i=0;;++i){\n')
    out_f.write('\t\tconst static_file_t* file = www_static_files[i];\n')
    out_f.write('\t\tif(!file){\n')
    out_f.write('\t\t\treturn nullptr;\n')
    out_f.write('\t\t}else{\n')
    out_f.write('\t\t\tif(strcmp(file->file_name, fileName) == 0){\n')
    out_f.write('\t\t\t\treturn file;\n')
    out_f.write('\t\t\t}\n')
    out_f.write('\t\t}\n')
    out_f.write('\t}\n')
    out_f.write('}\n\n')

    out_f.write('esp_err_t static_get_handler( httpd_req_t *req ){\n')
    out_f.write('\tconst static_file_t* fileInfo = getFileInfo(req->uri);\n')
    out_f.write('\tif(!fileInfo) return ESP_FAIL;\n')
    out_f.write('\tstd::string headerVal;\n')
    out_f.write('\tbool dontReload = false;\n')
    out_f.write('\tif(get_req_header("If-Modified-Since", headerVal, req) && (headerVal == fileInfo->last_modified)){\n')
    # out_f.write('\t\tESP_LOGI(TAG, "If-Modified-Since match");\n')
    out_f.write('\t\tdontReload = true;\n')
    out_f.write('\t}else if(get_req_header("If-None-Match", headerVal, req) && (headerVal == fileInfo->etag)){\n')
    # out_f.write('\t\tESP_LOGI(TAG, "If-None-Match match");\n')
    out_f.write('\t\tdontReload = true;\n')
    out_f.write('\t}\n')
    # out_f.write('\thttpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000" );\n')
    out_f.write('\thttpd_resp_set_hdr(req, "Last-Modified", fileInfo->last_modified );\n')
    out_f.write('\thttpd_resp_set_hdr(req, "ETag", fileInfo->etag );\n')
    out_f.write('\tif(dontReload){\n')
    out_f.write('\t\thttpd_resp_set_status( req, "304 Not Modified" );\n')
    out_f.write('\t\thttpd_resp_send(req, nullptr, 0);\n')
    out_f.write('\t}else{\n')
    out_f.write('\t\thttpd_resp_set_status( req, HTTPD_200 );\n')
    out_f.write('\t\thttpd_resp_set_hdr(req, "Content-Encoding", "gzip" );\n')
    out_f.write('\t\thttpd_resp_set_type(req, fileInfo->mimetype);\n')
    out_f.write('\t\thttpd_resp_send( req, (const char *)fileInfo->data, fileInfo->len);\n')
    out_f.write('\t}\n')
    out_f.write('\treturn ESP_OK;\n')
    out_f.write('}\n')

print('Content total bytes : ', totalBytes)
