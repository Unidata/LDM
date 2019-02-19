#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Copyright (C) 2019 University of Virginia. All rights reserved.

file      account.py
author    Yuanlong Tan <yt4xb@virginia.edu>
version   1.0
date      Feb. 14, 2019
brief     Read OESS API account
"""

import json
def readAccount(filename):
	f = open(filename, 'r+')
	jsonData = json.load(f)
	username = jsonData['username']
	passwd = jsonData['password']
	return (username,passwd)
