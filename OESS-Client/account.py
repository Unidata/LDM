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

import yaml
def readAccount(filename):
	f = open(filename, 'r+')
	yamlData = yaml.load(f)
	username = yamlData['username']
	passwd = yamlData['passwd']
	return (username,passwd)
