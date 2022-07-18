#
# Copyright (C) 2012 The Android Open Source Project
# Copyright (C) 2016-2018 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#


# Camera Metadata XML
## Introduction
This is a set of scripts to manipulate the camera metadata in an XML form.

## Generated Files
Metadata related source code can be generated from XML.

## Dependencies
* Python 2.7.x+
* Beautiful Soup 4+ - HTML/XML parser, used to parse `metadata_properties.xml`
* Mako 0.7+         - Template engine, needed to do file generation.
* Markdown 2.1+     - Plain text to HTML converter, for docs formatting.
* Tidy              - Cleans up the XML/HTML files.
* XML Lint          - Validates XML against XSD schema.

## Quick Setup (Ubuntu Precise):
sudo apt-get install python-mako
sudo apt-get install python-bs4
sudo apt-get install python-markdown
sudo apt-get install tidy
sudo apt-get install libxml2-utils #xmllint

## Quick Setup (MacPorts)
sudo port install py27-beautifulsoup4
sudo port install py27-mako
sudo port install py27-markdown
sudo port install tidy
sudo port install libxml2 #xmllint
