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

using std::string;
using std::vector;

jack_port_t *input_port;
jack_port_t *output_port;

double        mout = 0.0;
double        release_delay_ms = 0;
double        release_ms = 50;

struct Sample
{
  double        mix_freq;
  double        osc_freq;
  vector<float> pcm_data;
  int           instrument;
};

vector<Sample> samples;
int            instrument = 1;
bool           pedal_down = false;

struct Voice
{
  enum { UNUSED, ON, RELEASE_DELAY, FADE_OUT } state;

  double env;
  double pos;
  double frequency;
  double velocity;
  double rd_pos;
  bool   pedal;
  int note;

  Sample *sample;

  Voice()
    : state (UNUSED),
      pedal (false)
  {
  }
};

vector<Voice> voices (256);

float
freqFromNote (float note)
{
  return 440 * exp (log (2) * (note - 69) / 12.0);
}

bool
isNoteOn (const jack_midi_event_t& event)
{
  if ((event.buffer[0] & 0xf0) == 0x90)
    {
      if (event.buffer[2] != 0) /* note on with velocity 0 => note off */
        return true;
    }
  return false;
}

bool
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

class JackSampler
{
private:
  double jack_mix_freq;

protected:
  int process (jack_nframes_t nframes);
  static int jack_process (jack_nframes_t nframes, void *arg);

public:
  void init (jack_client_t *client);
};

void
JackSampler::init (jack_client_t *client)
{
  jack_set_process_callback (client, jack_process, this);

  jack_mix_freq = jack_get_sample_rate (client);
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

              int v = 0;
              while ((v < voices.size()) && (voices[v].state != Voice::ON || voices[v].note != in_event.buffer[1])) // FIXME: handle sustained notes
                v++;
              if (v != voices.size())
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
                          if (voices[v].pedal == true)
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

struct Options
{
  string program_name;
} options;

void
loadNote (int note, const char *file_name, int instrument)
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

int
parseConfig (int instrument, const char *name)
{
  FILE *config = fopen (name, "r");

  char buffer[1024];

  while (fgets (buffer, 1024, config))
    {
      const char *sep = " \t\n";
      const char *command = strtok (buffer, sep);
      if (command && strcmp (command, "sample") == 0)
        {
          const char *note_str = strtok (NULL, sep);

          if (note_str)
            {
              int note = atoi (note_str);
              const char *file = strtok (NULL, sep);
              loadNote (note, file, instrument);
              printf ("NOTE %d FILE %s\n", note, file);
            }
        }
      else if (command && strcmp (command, "release_delay") == 0)
        {
          const char *rd_str = strtok (NULL, sep);
          if (rd_str)
            {
              release_delay_ms = atof (rd_str);
              printf ("RELEASE_DELAY %f ms\n", release_delay_ms);
            }
        }
      else if (command && strcmp (command, "release") == 0)
        {
          const char *rd_str = strtok (NULL, sep);
          if (rd_str)
            {
              release_ms = atof (rd_str);
              printf ("RELEASE %f ms\n", release_ms);
            }
        }
    }
  return 0;
}

int
main (int argc, char **argv)
{
  /* init */
  SfiInitValue values[] = {
    { "stand-alone",            "true" }, /* no rcfiles etc. */
    { "wave-chunk-padding",     NULL, 1, },
    { "dcache-block-size",      NULL, 8192, },
    { "dcache-cache-memory",    NULL, 5 * 1024 * 1024, },
    { NULL }
  };
  bse_init_inprocess (&argc, &argv, NULL, values);
  //options.parse (&argc, &argv);
  options.program_name = "sampler";

  if (argc < 2)
    {
      fprintf (stderr, "usage: jacksampler <config1> [ <config2> ... <configN> ]\n");
      exit (1);
    }

  for (int i = 1; i < argc; i++)
    parseConfig (i, argv[i]);

  jack_client_t *client;
  client = jack_client_open ("sampler", JackNullOption, NULL);
  if (!client)
    {
      fprintf (stderr, "unable to connect to jack server\n");
      exit (1);
    }

  JackSampler jack_sampler;

  jack_sampler.init (client);


  //jack_set_sample_rate_callback (client, srate, 0);

  //jack_on_shutdown (client, jack_shutdown, 0);

  input_port = jack_port_register (client, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
  output_port = jack_port_register (client, "audio_out", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

  if (jack_activate (client))
    {
      fprintf(stderr, "cannot activate client");
      return 1;
    }

  while (1)
    {
      char buffer[1024];
      fgets (buffer, 1024, stdin);
      instrument = atoi (buffer);
      printf ("INSTRUMENT CHANGED TO %d\n", instrument);
    }
}
