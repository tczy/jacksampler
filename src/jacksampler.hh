/* JackSampler - JACK based sampler
 * Copyright (C) 2009-2010 Stefan Westerfeld
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef JACK_SAMPLER_JACK_SAMPLER_HH
#define JACK_SAMPLER_JACK_SAMPLER_HH

#include <jack/jack.h>
#include "main.hh"

struct Sample
{
  double             mix_freq;
  double             osc_freq;
  std::vector<float> pcm_data;
  int                instrument;
};

class JackSampler
{
protected:
  double       jack_mix_freq;

  jack_port_t *input_port;
  jack_port_t *output_port;

  std::vector<Sample> samples;

  void        load_note (const Options& options, int note, const char *file_name, int instrument);
  int         process (jack_nframes_t nframes);
  static int  jack_process (jack_nframes_t nframes, void *arg);

public:
  void init (jack_client_t *client);
  void parse_config (const Options& options, int instrument, const char *name);
  void change_instrument (int new_instrument);
};

#endif

