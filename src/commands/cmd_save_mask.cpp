/* ASE - Allegro Sprite Editor
 * Copyright (C) 2001-2011  David Capello
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <allegro/file.h>

#include "commands/command.h"
#include "dialogs/filesel.h"
#include "gui/alert.h"
#include "raster/mask.h"
#include "raster/sprite.h"
#include "document_wrappers.h"
#include "util/msk_file.h"

//////////////////////////////////////////////////////////////////////
// save_mask

class SaveMaskCommand : public Command
{
public:
  SaveMaskCommand();
  Command* clone() { return new SaveMaskCommand(*this); }

protected:
  bool onEnabled(Context* context);
  void onExecute(Context* context);
};

SaveMaskCommand::SaveMaskCommand()
  : Command("SaveMask",
	    "Save Mask",
	    CmdUIOnlyFlag)
{
}

bool SaveMaskCommand::onEnabled(Context* context)
{
  return context->checkFlags(ContextFlags::ActiveDocumentIsReadable);
}

void SaveMaskCommand::onExecute(Context* context)
{
  const ActiveDocumentReader document(context);
  base::string filename = "default.msk";
  int ret;

  for (;;) {
    filename = ase_file_selector("Save .msk File", filename, "msk");
    if (filename.empty())
      return;

    /* does the file exist? */
    if (exists(filename.c_str())) {
      /* ask if the user wants overwrite the file? */
      ret = Alert::show("Warning<<File exists, overwrite it?<<%s||&Yes||&No||&Cancel",
			get_filename(filename.c_str()));
    }
    else
      break;

    /* "yes": we must continue with the operation... */
    if (ret == 1)
      break;
    /* "cancel" or <esc> per example: we back doing nothing */
    else if (ret != 2)
      return;

    /* "no": we must back to select other file-name */
  }

  if (save_msk_file(document->getMask(), filename.c_str()) != 0)
    Alert::show("Error<<Error saving .msk file<<%s||&Close", filename.c_str());
}

//////////////////////////////////////////////////////////////////////
// CommandFactory

Command* CommandFactory::createSaveMaskCommand()
{
  return new SaveMaskCommand;
}
