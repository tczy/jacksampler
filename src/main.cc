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
#include <bse/bsemain.h>
#include <stdio.h>
#include <stdlib.h>

#include "main.hh"
#include "jacksampler.hh"


int
main (int argc, char **argv)
{
  Options options;
  JackSampler jack_sampler;

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
    jack_sampler.parse_config (options, i, argv[i]);

  jack_client_t *client;
  client = jack_client_open ("sampler", JackNullOption, NULL);
  if (!client)
    {
      fprintf (stderr, "unable to connect to jack server\n");
      exit (1);
    }

  jack_sampler.init (client);
  //jack_set_sample_rate_callback (client, srate, 0);
  //jack_on_shutdown (client, jack_shutdown, 0);

  while (1)
    {
      char buffer[1024];
      fgets (buffer, 1024, stdin);
      int instrument = atoi (buffer);
      jack_sampler.change_instrument (instrument);
    }
}
