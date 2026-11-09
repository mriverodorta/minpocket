#!/usr/bin/env python3
"""Fail closed on private paths, binaries, image assets and obvious credentials.

Inspect the Git index and non-ignored source files. Also inspect historical
blobs when commits exist. Findings print file names and rule IDs, never values.
"""
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
TOP={'.gitignore','.gitattributes','.githooks','.github','README.md','LICENSE',
     'THIRD_PARTY.md','Makefile','native','boot','design','production','scripts'}
DENY_PARTS={'localdev','provisioning','research','experiments','concepts','screenshots',
            'games','.ssh','.impeccable','__pycache__'}
DENY_NAMES={'authorized_keys','known_hosts','.DS_Store','sc.fr.env','home-art.h',
            'preview-art.h','background-art.h','preview-cards.h'}
DENY_SUFFIX=re.compile(r'\.(?:png|jpe?g|bmp|gif|webp|avif|svg|mp4|mov|img|fit|dtb|cpio|tar|zip|gz|xz|mps|mpa|rgba|o|so|dylib|a|log|pem|key|pyc)(?:\.|$)',re.I)
PATTERNS={
 'private-key':re.compile(rb'(?m)^-----BEGIN (?:OPENSSH |RSA |EC |DSA |ENCRYPTED )?PRIVATE KEY-----'),
 'personal-path':re.compile(rb'/(?:Users|home)/[A-Za-z0-9_.-]+/'),
 'private-device-address':re.compile(rb'\b10\.10\.10\.[0-9]{1,3}\b'),
 'github-token':re.compile(rb'\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,})\b'),
 'cloud-key':re.compile(rb'\b(?:AKIA|ASIA)[A-Z0-9]{16}\b'),
 'literal-password':re.compile(rb'''(?im)^\s*(?:export\s+)?(?:SCREENSCRAPER_PASSWORD|SSPASSWORD|DEVPASSWORD|WIFI_PASSWORD|WPA_PSK|PASSWORD)\s*=\s*['"]?[^\s'"$<{][^\r\n]{5,}'''),
}
MAGIC=(b'\x7fELF',b'\x89PNG',b'\xff\xd8\xff',b'PK\x03\x04',b'\x1f\x8b',b'\xcf\xfa\xed\xfe',b'\xca\xfe\xba\xbe')
def path_rules(name):
 p=PurePosixPath(name);parts=p.parts
 if not parts or p.is_absolute() or '..' in parts or parts[0] not in TOP:return ['unexpected-path']
 rules=[]
 if any(part in DENY_PARTS for part in parts):rules.append('private-directory')
 if p.name in DENY_NAMES and name!='native/tests/preview-cards.h':rules.append('private-asset-or-identity')
 if p.name=='.env' or '.env.' in p.name or p.name.endswith('.env'):rules.append('environment-file')
 if DENY_SUFFIX.search(p.name):rules.append('binary-or-media-extension')
 return rules
def content_rules(data):
 rules=[]
 if len(data)>1024*1024:rules.append('oversized-source')
 if data.startswith(MAGIC) or b'\0' in data:rules.append('binary-content')
 for name,pattern in PATTERNS.items():
  if pattern.search(data):rules.append(name)
 return rules
def git(*args):return subprocess.run(['git','-C',str(ROOT),*args],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
def audit():
 indexed=git('ls-files','--cached','--stage','-z')
 failures=[];count=0
 if indexed.returncode==0:
  entries=[]
  for item in indexed.stdout.split(b'\0'):
   if not item:continue
   mode_blob,name=item.split(b'\t',1);mode,blob,stage=mode_blob.split()
   name=name.decode();data=git('cat-file','blob',blob.decode()).stdout;entries.append((name,data))
   if mode not in [b'100644',b'100755'] or stage!=b'0':failures.append((name,'symlink-submodule-or-conflict'))
   working=ROOT/name
   if working.is_symlink():failures.append((name,'working-tree-symlink'))
   elif working.is_file() and working.read_bytes()!=data:entries.append((name,working.read_bytes()))
  extra=git('ls-files','--others','--exclude-standard','-z').stdout
  for name in extra.split(b'\0'):
   if name:
    p=ROOT/name.decode()
    if p.is_symlink():failures.append((name.decode(),'symlink'))
    elif p.is_file():entries.append((name.decode(),p.read_bytes()))
 else:
  entries=[(str(p.relative_to(ROOT)),p.read_bytes()) for p in ROOT.rglob('*')
           if p.is_file() and not p.is_symlink() and not any(x in {'build','.git'} for x in p.relative_to(ROOT).parts)]
 for name,data in entries:
  count+=1
  failures.extend((name,rule) for rule in path_rules(name)+content_rules(data))
 history=git('rev-list','--objects','--all')
 historical=0
 if history.returncode==0:
  for line in history.stdout.decode().splitlines():
   fields=line.split(' ',1)
   if len(fields)!=2:continue
   blob,name=fields;kind=git('cat-file','-t',blob)
   if kind.stdout.strip()!=b'blob':continue
   historical+=1;data=git('cat-file','blob',blob).stdout
   failures.extend(('history:'+name,rule) for rule in path_rules(name)+content_rules(data))
 for name,rule in sorted(set(failures)):print(f'{rule}: {name}',file=sys.stderr)
 if failures:return 1
 print(f'Public-source audit passed: {count} files, {historical} historical blobs; no prohibited content found.')
 return 0
if __name__=='__main__':sys.exit(audit())
