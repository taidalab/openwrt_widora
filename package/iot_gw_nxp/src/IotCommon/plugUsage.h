// ------------------------------------------------------------------
// Plug Usage include file
// ------------------------------------------------------------------
// Calculates plug usage from iot_plughistory database
// ------------------------------------------------------------------
// Author:    nlv10677
// Copyright: NXP B.V. 2014. All rights reserved
// ------------------------------------------------------------------

// ------------------------------------------------------------------
// Cleanup
// ------------------------------------------------------------------

int plugHistoryCleanup( char * mac, int now );

// ------------------------------------------------------------------
// Wh calculation
// ------------------------------------------------------------------

int plugFindHourUsage( char * mac, int now );
int plugFindDayUsage( char * mac, int now );

// ------------------------------------------------------------------
// History
// ------------------------------------------------------------------

int * plugGetHistory( char * mac, int now, int period, int num, int * buffer );

