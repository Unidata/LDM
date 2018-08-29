/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: Task.cpp
 * @author: Steven R. Emmerson
 *
 * This file implements a task that will be executed on an independent thread.
 */

#include "Task.h"

Task::~Task() {};       // called by subclasses => must exist
