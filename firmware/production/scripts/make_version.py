Import("env")
import subprocess

def getCommand(cmd):
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        return ''
    else:
        return result.stdout.strip()

def getDiff():
    result = subprocess.run(["git", "diff", "--quiet", "--exit-code"])
    if result.returncode != 0:
        return '+'
    else:
        return ''

git_rev = getCommand(["git", "log", "--pretty=format:%h", "-n1"])
git_diff = getDiff()
git_tag = 'N/A'
git_branch = 'N/A'

if not git_rev:
    git_rev = 'N/A'
else:
    git_rev = git_rev[:7]
    git_tag = getCommand(["git", "describe", "--exact-match", "--tags"])
    git_branch = getCommand(["git", "rev-parse", "--abbrev-ref", "HEAD"])

git_version = f'Git~{git_rev}{git_diff}'
if git_tag:
    git_version = git_tag

print('Git rev : ' + git_rev)
print('Git diff : ' + git_diff)
print('Git tag : ' + git_tag)
print('Git branch : ' + git_branch)
env.Append(GIT_VER=git_version)

with open('./src/GitVersion.cpp', 'w') as out_f:
    out_f.write('#include <GitVersion.hpp>\n\n')
    out_f.write(f'const char* GIT_REV="{git_rev}";\n')
    out_f.write(f'const char* GIT_TAG="{git_tag}";\n')
    out_f.write(f'const char* GIT_VERSION="{git_version}";\n')
    out_f.write(f'const char* GIT_BRANCH="{git_branch}";\n')
