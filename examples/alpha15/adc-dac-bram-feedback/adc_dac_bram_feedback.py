#!/usr/bin/env python
# -*- coding: utf-8 -*-

import time
import math
import numpy as np

from koheron import command

class AdcDacBram(object):
    def __init__(self, client):
        self.client = client
        self.dac_size = self.get_dac_size()
        self.dac = np.zeros((2, self.dac_size))
        self.adc_size = self.get_adc_size()
        self.adc = np.zeros((2, self.adc_size))

    @command()
    def get_dac_size(self):
        return self.client.recv_uint32()

    @command()
    def get_adc_size(self):
        return self.client.recv_uint32()

    @command()
    def trigger_acquisition(self):
        pass

    def set_dac(self):
        @command()
        def set_dac_data(self, data):
            pass
        # Conversion to two's complement:
        data1 = np.uint32(np.mod(np.floor(32768 * self.dac[0, :]) + 32768, 65536) + 32768)
        data2 = np.uint32(np.mod(np.floor(32768 * self.dac[1, :]) + 32768, 65536) + 32768)
        set_dac_data(self, data1 + (data2 << 16))

    def _to_two_complement(self, data):
        nbits = 18
        nmax = 2**nbits
        return (np.int32(data % nmax) - nmax/2) % nmax - nmax/2

    @command()
    def get_adc(self, channel):
        data = self.client.recv_array(self.adc_size, dtype='uint32')
        # self.adc[channel,:] = data
        self.adc[channel,:] = self._to_two_complement(data)

    @command(classname='ClockGenerator')
    def phase_shift(self, shift):
        pass

    @command(classname='ClockGenerator')
    def set_sampling_frequency(self, val):
        pass
    @command(classname='ClockGenerator')
    def set_reference_clock(self, clkin):
        pass

    @command(classname='Ltc2387')
    def adc_raw_data(self, n_avg):
        return self.client.recv_array(2, dtype='int32')

    @command(classname='Ltc2387')
    def adc_data_volts(self, n_avg):
        return self.client.recv_array(2, dtype='float32')

    @command(classname='Ltc2387')
    def enable_adcs(self):
        pass

    @command(classname='Ltc2387')
    def set_testpat(self):
        pass

    @command(classname='Ltc2387')
    def range_select(self, channel, range):
        # range = 0 => 2V
        # range = 1 => 8V
        pass

    @command(classname='Ltc2387')
    def input_range(self, channel):
        return self.client.recv_uint32()

    @command(classname='Ltc2387')
    def clear_testpat(self):
        pass

    @command(classname='Ltc2387')
    def clkout_dec(self):
        pass

    @command(classname='Ltc2387')
    def range_select(self, channel, range):
        pass

    @command(classname='Ltc2387')
    def set_clock_delay(self, channel):
        pass

    @command(classname='Ltc2387')
    def dco_delay_tap(self, channel, tap):
        pass

    @command(classname='Ltc2387')
    def da_delay_tap(self, channel, tap):
        pass

    @command(classname='Ltc2387')
    def db_delay_tap(self, channel, tap):
        pass