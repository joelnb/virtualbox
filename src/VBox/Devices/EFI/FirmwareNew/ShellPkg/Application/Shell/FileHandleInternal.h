/** @file
  internal worker functions for FileHandleWrappers to use

  Copyright (c) 2009 - 2010, Intel Corporation. All rights reserved.<BR>
  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#ifndef _FILE_HANDLE_INTERNAL_HEADER_
#define _FILE_HANDLE_INTERNAL_HEADER_

/**
  Move the cursor position one character backward.

  @param[in] LineLength       Length of a line. Get it by calling QueryMode
  @param[in, out] Column      Current column of the cursor position
  @param[in, out] Row         Current row of the cursor position
**/
VOID
MoveCursorBackward (
  IN     UINTN                   LineLength,
  IN OUT UINTN                   *Column,
  IN OUT UINTN                   *Row
  );

/**
  Move the cursor position one character forward.

  @param[in] LineLength       Length of a line.
  @param[in] TotalRow         Total row of a screen
  @param[in, out] Column      Current column of the cursor position
  @param[in, out] Row         Current row of the cursor position
**/
VOID
MoveCursorForward (
  IN     UINTN                   LineLength,
  IN     UINTN                   TotalRow,
  IN OUT UINTN                   *Column,
  IN OUT UINTN                   *Row
  );

/**
  Prints out each previously typed command in the command list history log.

  When each screen is full it will pause for a key before continuing.

  @param[in] TotalCols    How many columns are on the screen
  @param[in] TotalRows    How many rows are on the screen
  @param[in] StartColumn  which column to start at
**/
VOID
PrintCommandHistory (
  IN CONST UINTN TotalCols,
  IN CONST UINTN TotalRows,
  IN CONST UINTN StartColumn
  );

#endif //_FILE_HANDLE_INTERNAL_HEADER_

