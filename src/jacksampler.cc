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
#include <sfi/sfistore.h>
#include <bse/bsemain.h>
#include <bse/bseloader.h>
#include <bse/gslfft.h>
#include <bse/bsemathsignal.h>
#include <bse/bseblockutils.hh>
#include <list>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <bse/gsldatautils.h>

#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <string>
#include <vector>

#include <jack/jack.h>
#include <jack/midiport.h>

#include "main.hh"
#include "jacksampler.hh"
#include "microconf.hh"

using std::string;
using std::vector;

static float
freqFromNote (float note)
{
  return 440 * exp (log (2) * (note - 69) / 12.0);
}

static bool
isNoteOn (const jack_midi_event_t& event)
{
  if ((event.buffer[0] & 0xf0) == 0x90)
    {
      if (event.buffer[2] != 0) /* note on with velocity 0 => note off */
        return true;
    }
  return false;
}

static bool
isNoteOff (const jack_midi_event_t& event)
{
  if ((event.buffer[0] & 0xf0) == 0x90)
    {
      if (event.buffer[2] == 0) /* note on with velocity 0 => note off */
        return true;
    }
  else if ((event.buffer[0] & 0xf0) == 0x80)
    {
      return true;
    }
  return false;
}

JackSampler::JackSampler() :
  voices (256),
  instrument (1),
  pedal_down (false),
  release_delay_ms (0),
  release_ms (50),
  mout (0),
  instrument_count (0)
{
}

void
JackSampler::init (const Options& options, jack_client_t *client, int argc, char **argv)
{
  for (int i = 1; i < argc; i++)
    parse_config (options, i, argv[i]);
  instrument_count = argc - 1;

  jack_set_process_callback (client, jack_process, this);

  jack_mix_freq = jack_get_sample_rate (client);

  input_port = jack_port_register (client, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
  output_port = jack_port_register (client, "audio_out", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

  if (jack_activate (client))
    {
      fprintf (stderr, "cannot activate client");
      exit (1);
    }
}

int
JackSampler::process (jack_nframes_t nframes)
{
  jack_default_audio_sample_t *out = (jack_default_audio_sample_t *) jack_port_get_buffer (output_port, nframes);
  void* port_buf = jack_port_get_buffer (input_port, nframes);
  jack_nframes_t event_count = jack_midi_get_event_count (port_buf);
  jack_midi_event_t in_event;
  jack_nframes_t event_index = 0;

  jack_midi_event_get (&in_event, port_buf, 0);
  for (int i = 0; i < nframes; i++)
    {
      while ((in_event.time == i) && (event_index < event_count))
        {
          // process event
          if (isNoteOn (in_event))
            {
              /* note on */
              //printf ("note on %d mout = %f\n", in_event.buffer[1], mout);
              /* find unused voice */
              int v = 0;
              while ((v < voices.size()) && (voices[v].state != Voice::UNUSED))
                v++;
              if (v != voices.size())
                {
                  //printf ("voice = %d\n", v);
                  voices[v].note  = in_event.buffer[1];
                  voices[v].env   = 1.0;
                  voices[v].pos   = 0.0;
                  voices[v].frequency = freqFromNote (in_event.buffer[1]);
                  voices[v].velocity = in_event.buffer[2] / 127.0;
                  voices[v].sample = NULL;
                  float best_delta = 1e20;
                  for (size_t s = 0; s < samples.size(); s++)
                    {
                      // find nearest sample with a logarithmic distance measure: delta of 220 and 440 == delta of 440 and 880 == 2
                      float delta = std::max (voices[v].frequency / samples[s].osc_freq, samples[s].osc_freq / voices[v].frequency);
                      if (samples[s].instrument == instrument && delta < best_delta)
                        {
                          voices[v].sample = &samples[s];
                          best_delta = delta;
                        }
                    }
                  if (voices[v].sample) /* only switch to on if a sample was found */
                    voices[v].state = Voice::ON;
                }
              else
                {
                  printf ("NO MORE VOICES\n");
                }
            }
          else if (isNoteOff (in_event))
            {
              /* note on */
              //printf ("note off %d\n", in_event.buffer[1]);

              for (int v = 0; v < voices.size(); v++)
                {
                  if (voices[v].state == Voice::ON && voices[v].note == in_event.buffer[1])
                    {
                      if (pedal_down)
                        voices[v].pedal = true;
                      else
                        {
                          //printf ("off voice %d\n", v);
                          voices[v].state = Voice::RELEASE_DELAY;
                          voices[v].rd_pos = 0;
                        }
                    }
                }
            }
          else if ((in_event.buffer[0] & 0xf0) == 0xb0)
            {
              //printf ("got midi controller event status=%x controller=%x value=%x\n",
              //        in_event.buffer[0], in_event.buffer[1], in_event.buffer[2]);
              if (in_event.buffer[1] == 0x40)
                {
                  pedal_down = in_event.buffer[2] > 0x40;
                  if (!pedal_down)
                    {
                      /* release voices which are sustained due to the pedal */
                      for (int v = 0; v < voices.size(); v++)
                        {
                          if (voices[v].pedal)
                            {
                              voices[v].state = Voice::RELEASE_DELAY;
                              voices[v].rd_pos = 0;
                              voices[v].pedal = false;
                            }
                        }
                    }
                }
            }

          // get next event
          event_index++;
          if (event_index < event_count)
            jack_midi_event_get (&in_event, port_buf, event_index);
        }

      // generate one sample
      out[i] = 0.0;
      for (int v = 0; v < voices.size(); v++)
        {
          if (voices[v].state != Voice::UNUSED)
            {
              voices[v].pos += voices[v].frequency / voices[v].sample->osc_freq *
                               voices[v].sample->mix_freq / jack_mix_freq;

              int ipos = voices[v].pos;
              double dpos = voices[v].pos - ipos;
              if (ipos < (voices[v].sample->pcm_data.size() - 1))
                {
                  double left = voices[v].sample->pcm_data[ipos];
                  double right = voices[v].sample->pcm_data[ipos + 1];
                  out[i] += (left * (1.0 - dpos) + right * dpos) * voices[v].env * voices[v].velocity;
                }
            }
          if (voices[v].state == Voice::RELEASE_DELAY)
            {
              voices[v].rd_pos += 1000.0 / jack_mix_freq;
              if (voices[v].rd_pos > release_delay_ms)
                voices[v].state = Voice::FADE_OUT;
            }
          if (voices[v].state == Voice::FADE_OUT)
            {
              voices[v].env -= (1000.0 / jack_mix_freq) / release_ms;
              if (voices[v].env <= 0)
                {
                  voices[v].state = Voice::UNUSED;
                  voices[v].pedal = false;
                }
            }
        }
      out[i] *= 0.333;    /* empiric */
      mout = std::max (fabs (out[i]), mout);
    }
  return 0;
}

int
JackSampler::jack_process (jack_nframes_t nframes, void *arg)
{
  JackSampler *instance = reinterpret_cast<JackSampler *> (arg);
  return instance->process (nframes);
}

void
JackSampler::load_note (const Options& options, int note, const char *file_name, int instrument)
{
  /* open input */
  BseErrorType error;

  BseWaveFileInfo *wave_file_info = bse_wave_file_info_load (file_name, &error);
  if (!wave_file_info)
    {
      fprintf (stderr, "%s: can't open the input file %s: %s\n", options.program_name.c_str(), file_name, bse_error_blurb (error));
      exit (1);
    }

  BseWaveDsc *waveDsc = bse_wave_dsc_load (wave_file_info, 0, FALSE, &error);
  if (!waveDsc)
    {
      fprintf (stderr, "%s: can't open the input file %s: %s\n", options.program_name.c_str(), file_name, bse_error_blurb (error));
      exit (1);
    }

  GslDataHandle *dhandle = bse_wave_handle_create (waveDsc, 0, &error);
  if (!dhandle)
    {
      fprintf (stderr, "%s: can't open the input file %s: %s\n", options.program_name.c_str(), file_name, bse_error_blurb (error));
      exit (1);
    }

  error = gsl_data_handle_open (dhandle);
  if (error)
    {
      fprintf (stderr, "%s: can't open the input file %s: %s\n", options.program_name.c_str(), file_name, bse_error_blurb (error));
      exit (1);
    }

  Sample s;

  vector<float> block (1024);
  uint64 pos = 0;
  uint64 len = gsl_data_handle_length (dhandle);
  while (pos < len)
    {
      uint64 r = gsl_data_handle_read (dhandle, pos, block.size(), &block[0]);

      for (int i = 0; i < r; i++)
        s.pcm_data.push_back (block[i]);
      pos += r;
    }
  printf ("loaded sample, length = %ld\n", s.pcm_data.size());
  s.mix_freq = gsl_data_handle_mix_freq (dhandle);
  s.osc_freq = freqFromNote (note);
  s.instrument = instrument;
  samples.push_back (s);
}

void
JackSampler::parse_config (const Options& options, int instrument, const char *name)
{
  MicroConf cfg (name);

  while (cfg.next())
    {
      int    note = 0;
      double d = 0;
      string file;

      if (cfg.command ("sample", note, file))
        {
          load_note (options, note, file.c_str(), instrument);
          printf ("NOTE %d FILE %s\n", note, file.c_str());
        }
      else if (cfg.command ("release_delay", d))
        {
          release_delay_ms = d;
          printf ("RELEASE_DELAY %f ms\n", release_delay_ms);
        }
      else if (cfg.command ("release", d))
        {
          release_ms = d;
          printf ("RELEASE %f ms\n", release_ms);
        }
      else
        {
          cfg.die_if_unknown();
        }
    }
}

void
JackSampler::change_instrument (int new_instrument)
{
  instrument = new_instrument;
  printf ("JackSampler: changed instrument to %d\n", instrument);
}

void
JackSampler::status()
{
  int unused = 0;
  int on = 0;
  int release_delay = 0;
  int fade_out = 0;

  for (size_t i = 0; i < voices.size(); i++)
    {
      if (voices[i].state == Voice::UNUSED)
        unused++;
      else if (voices[i].state == Voice::ON)
        on++;
      else if (voices[i].state == Voice::RELEASE_DELAY)
        release_delay++;
      else if (voices[i].state == Voice::FADE_OUT)
        fade_out++;
      else
        g_assert_not_reached();
    }
  printf ("sampling rate:   %.2f\n", jack_mix_freq);
  printf ("instruments:     %d\n", instrument_count);
  printf ("active instr.:   %d\n", instrument);
  printf ("total voices:    %d\n", voices.size());
  printf ("\n");
  printf (" * unused        %d\n", unused);
  printf (" * on            %d\n", on);
  printf (" * release delay %d\n", release_delay);
  printf (" * fade out      %d\n", fade_out);
}

void
JackSampler::reset()
{
  // reset all voices the hard way (might click)
  for (size_t v = 0; v < voices.size(); v++)
    voices[v].state = Voice::UNUSED;
}
