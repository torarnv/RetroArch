/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2015 - Daniel De Matteis
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boolean.h>

#include <rthreads/rthreads.h>

#include "general.h"

struct autosave
{
   volatile bool quit;
   slock_t *lock;

   slock_t *cond_lock;
   scond_t *cond;
   sthread_t *thread;

   void *buffer;
   const void *retro_buffer;
   const char *path;
   size_t bufsize;
   unsigned interval;
};

/**
 * autosave_lock:
 * @handle          : pointer to autosave object
 *
 * Locks autosave.
 **/
static void autosave_lock(autosave_t *handle)
{
   slock_lock(handle->lock);
}

/**
 * autosave_unlock:
 * @handle          : pointer to autosave object
 *
 * Unlocks autosave.
 **/
static void autosave_unlock(autosave_t *handle)
{
   slock_unlock(handle->lock);
}

/**
 * autosave_thread:
 * @data            : pointer to autosave object
 *
 * Callback function for (threaded) autosave.
 **/
static void autosave_thread(void *data)
{
   bool first_log = true;
   autosave_t *save = (autosave_t*)data;

   while (!save->quit)
   {
      bool differ;

      autosave_lock(save);
      differ = memcmp(save->buffer, save->retro_buffer,
            save->bufsize) != 0;
      if (differ)
         memcpy(save->buffer, save->retro_buffer, save->bufsize);
      autosave_unlock(save);

      if (differ)
      {
         /* Should probably deal with this more elegantly. */
         FILE *file = fopen(save->path, "wb");

         if (file)
         {
            bool failed = false;

            /* Avoid spamming down stderr ... */
            if (first_log)
            {
               RARCH_LOG("Autosaving SRAM to \"%s\", will continue to check every %u seconds ...\n",
                     save->path, save->interval);
               first_log = false;
            }
            else
               RARCH_LOG("SRAM changed ... autosaving ...\n");

            failed |= fwrite(save->buffer, 1, save->bufsize, file)
               != save->bufsize;
            failed |= fflush(file) != 0;
            failed |= fclose(file) != 0;
            if (failed)
               RARCH_WARN("Failed to autosave SRAM. Disk might be full.\n");
         }
      }

      slock_lock(save->cond_lock);

      if (!save->quit)
         scond_wait_timeout(save->cond, save->cond_lock,
               save->interval * 1000000LL);

      slock_unlock(save->cond_lock);
   }
}

/**
 * autosave_new:
 * @path            : path to autosave file
 * @data            : pointer to buffer
 * @size            : size of @data buffer
 * @interval        : interval at which saves should be performed.
 *
 * Create and initialize autosave object.
 *
 * Returns: pointer to new autosave_t object if successful, otherwise
 * NULL.
 **/
autosave_t *autosave_new(const char *path, const void *data, size_t size,
      unsigned interval)
{
   autosave_t *handle = (autosave_t*)calloc(1, sizeof(*handle));
   if (!handle)
      return NULL;

   handle->bufsize      = size;
   handle->interval     = interval;
   handle->path         = path;
   handle->buffer       = malloc(size);
   handle->retro_buffer = data;

   if (!handle->buffer)
   {
      free(handle);
      return NULL;
   }
   memcpy(handle->buffer, handle->retro_buffer, handle->bufsize);

   handle->lock         = slock_new();
   handle->cond_lock    = slock_new();
   handle->cond         = scond_new();

   handle->thread       = sthread_create(autosave_thread, handle);

   return handle;
}

/**
 * autosave_free:
 * @handle          : pointer to autosave object
 *
 * Frees autosave object.
 **/
void autosave_free(autosave_t *handle)
{
   if (!handle)
      return;

   slock_lock(handle->cond_lock);
   handle->quit = true;
   slock_unlock(handle->cond_lock);
   scond_signal(handle->cond);
   sthread_join(handle->thread);

   slock_free(handle->lock);
   slock_free(handle->cond_lock);
   scond_free(handle->cond);

   free(handle->buffer);
   free(handle);
}

/**
 * lock_autosave:
 *
 * Lock autosave.
 **/
void lock_autosave(void)
{
   unsigned i;
   global_t *global = global_get_ptr();

   for (i = 0; i < global->autosave.num; i++)
   {
      if (global->autosave.list[i])
         autosave_lock(global->autosave.list[i]);
   }
}

/**
 * unlock_autosave:
 *
 * Unlocks autosave.
 **/
void unlock_autosave(void)
{
   unsigned i;
   global_t *global = global_get_ptr();

   for (i = 0; i < global->autosave.num; i++)
   {
      if (global->autosave.list[i])
         autosave_unlock(global->autosave.list[i]);
   }
}


