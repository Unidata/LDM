#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Copyright (C) 2019 University of Virginia. All rights reserved.

file      remove.py
author    Yuanlong Tan <yt4xb@virginia.edu>
version   1.0
date      Mar. 1, 2019
brief     remove entry from the circuits
"""
import urllib2
import urllib
import sys
import string
import json
import account
import edit
from subprocess import call
global values1
values1 = {}
(username,passwd)=account.readAccount(sys.argv[2])
wg_id=account.getWkGpID(sys.argv[1],username,passwd)
ct_id=account.getCtID(wg_id, sys.argv[3],username,passwd)
searchResults = edit.edit_endpoint(wg_id,sys.argv[4],sys.argv[5],sys.argv[6],ct_id,"del",username,passwd)

if (searchResults == None):
	call(["ulogger", "-i","-l","$LDM/var/logs/ldmd.log",jsonData['error_text']])
else:
	call(["ulogger", "-i","-l","$LDM/var/logs/ldmd.log","Remove.py: Success"])
