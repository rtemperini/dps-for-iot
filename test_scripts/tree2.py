#!/usr/bin/python

from common import *
import atexit

atexit.register(cleanup)

#
#           /---> B ----\
#          /             \
#         /               \
#       A ------> C -------> E
#         \               /
#          \             /
#           \---> D-----/
#
#

sub1 = sub('E')

sub2 = sub('-l 40001 -p {} B'.format(sub1.port))
sub3 = sub('-l 40002 -p {} C'.format(sub1.port))
sub4 = sub('-l 40003 -p {} D'.format(sub1.port))

sub5 = sub('-p {} -p {} -p {} A'.format(sub2.port, sub3.port, sub4.port))
