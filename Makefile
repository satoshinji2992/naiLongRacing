############################################################################
# apps/examples/Racing/Makefile
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License");  You may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################

include $(APPDIR)/Make.defs

PROGNAME = racing
PRIORITY = $(CONFIG_EXAMPLES_RACING_PRIORITY)
STACKSIZE = $(CONFIG_EXAMPLES_RACING_STACKSIZE)
MODULE = $(CONFIG_EXAMPLES_RACING)

# Add include path for headers
CFLAGS += -I$(APPDIR)/examples/Racing/include

# Game core
CSRCS += src/game.c

# Framebuffer renderer + bitmap font (no LVGL)
CSRCS += src/render_fb.c
CSRCS += src/font.c

# Touch + GPIO input handler
CSRCS += src/racing_input.c
CSRCS += src/jy60.c
CSRCS += src/racing_voice.c

# Main entry
MAINSRC = src/racing_main.c

include $(APPDIR)/Application.mk
