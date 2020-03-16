//
// Printer object for LPrint, a Label Printer Application
//
// Copyright © 2019-2020 by Michael R Sweet.
// Copyright © 2010-2019 by Apple Inc.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

//
// Include necessary headers...
//

#include "pappl-private.h"


//
// Local functions...
//

static int	compare_active_jobs(pappl_job_t *a, pappl_job_t *b);
static int	compare_all_jobs(pappl_job_t *a, pappl_job_t *b);
static int	compare_completed_jobs(pappl_job_t *a, pappl_job_t *b);
static int	compare_printers(pappl_printer_t *a, pappl_printer_t *b);
static void	free_printer(pappl_printer_t *printer);


//
// 'papplPrinterCreate()' - Create a new printer.
//

pappl_printer_t *			// O - Printer
papplPrinterCreate(
    pappl_system_t *system,		// I - System
    int            printer_id,		// I - printer-id value or 0 for new
    const char     *printer_name,	// I - Printer name
    const char     *driver_name,	// I - Driver name
    const char     *device_uri)		// I - Device URI
{
  pappl_printer_t	*printer;	// Printer
  char			resource[1024],	// Resource path
			uuid[128];	// printer-uuid
  int			k_supported;	// Maximum file size supported
  int			num_formats;	// Number of supported document formats
  const char		*formats[10];	// Supported document formats
  struct statfs		spoolinfo;	// FS info for spool directory
  double		spoolsize;	// FS size
  static const char * const ipp_versions[] =
  {					// ipp-versions-supported values
    "1.1",
    "2.0"
  };
  static const char * const ipp_features[] =
  {					// ipp-features-supported values
    "ipp-everywhere"
  };
  static const int	operations[] =	// operations-supported values
  {
    IPP_OP_PRINT_JOB,
    IPP_OP_VALIDATE_JOB,
    IPP_OP_CREATE_JOB,
    IPP_OP_SEND_DOCUMENT,
    IPP_OP_CANCEL_JOB,
    IPP_OP_GET_JOB_ATTRIBUTES,
    IPP_OP_GET_JOBS,
    IPP_OP_GET_PRINTER_ATTRIBUTES,
    IPP_OP_SET_PRINTER_ATTRIBUTES,
    IPP_OP_CANCEL_MY_JOBS,
    IPP_OP_CLOSE_JOB,
    IPP_OP_IDENTIFY_PRINTER
  };
  static const char * const charset[] =	// charset-supported values
  {
    "us-ascii",
    "utf-8"
  };
  static const char * const compression[] =
  {					// compression-supported values
    "deflate",
    "gzip",
    "none"
  };
  static const char * const identify_actions[] =
  {					// identify-actions-supported values
    "display",
    "sound"
  };
  static const char * const job_creation_attributes[] =
  {					// job-creation-attributes-supported values
    "copies",
    "document-format",
    "document-name",
    "ipp-attribute-fidelity",
    "job-name",
    "job-priority",
    "media",
    "media-col",
    "multiple-document-handling",
    "orientation-requested",
    "print-color-mode",
    "print-content-optimize",
    "print-darkness",
    "print-quality",
    "print-speed",
    "printer-resolution"
  };
  static const char * const media_col[] =
  {					// media-col-supported values
    "media-bottom-margin",
    "media-left-margin",
    "media-right-margin",
    "media-size",
    "media-size-name",
    "media-source",
    "media-top-margin",
    "media-top-offset",
    "media-tracking",
    "media-type"
  };
  static const char * const multiple_document_handling[] =
  {					// multiple-document-handling-supported values
    "separate-documents-uncollated-copies",
    "separate-documents-collated-copies"
  };
  static const int orientation_requested[] =
  {
    IPP_ORIENT_PORTRAIT,
    IPP_ORIENT_LANDSCAPE,
    IPP_ORIENT_REVERSE_LANDSCAPE,
    IPP_ORIENT_REVERSE_PORTRAIT,
    IPP_ORIENT_NONE
  };
  static const char * const print_color_mode[] =
  {					// print-color-mode-supported
    "bi-level",
    "monochrome"
  };
  static const char * const print_content_optimize[] =
  {					// print-content-optimize-supported
    "auto",
    "graphic",
    "photo",
    "text-and-graphic",
    "text"
  };
  static const int print_quality[] =	// print-quality-supported
  {
    IPP_QUALITY_DRAFT,
    IPP_QUALITY_NORMAL,
    IPP_QUALITY_HIGH
  };
  static const char * const printer_kind[] =
  {					// printer-kind values
    "labels",
    "receipt"
  };
  static const char * const printer_settable_attributes[] =
  {					// printer-settable-attributes values
    "copies-default",
    "document-format-default",
    "label-mode-configured",
    "label-tear-off-configured",
    "media-col-default",
    "media-col-ready",
    "media-default",
    "media-ready",
    "multiple-document-handling-default",
    "orientation-requested-default",
    "print-color-mode-default",
    "print-content-optimize-default",
    "print-darkness-default",
    "print-quality-default",
    "print-speed-default",
    "printer-darkness-configured",
    "printer-geo-location",
    "printer-location",
    "printer-organization",
    "printer-organizational-unit",
    "printer-resolution-default"
  };
  static const char * const printer_strings_languages[] =
  {					// printer-strings-languages-supported values
    "de",
    "en",
    "es",
    "fr",
    "it"
  };
  static const char * const uri_authentication[] =
  {					// uri-authentication-supported values
    "none",
    "basic"
  };
  static const char * const uri_security[] =
  {					// uri-security-supported values
    "none",
    "tls"
  };
  static const char * const which_jobs[] =
  {					// which-jobs-supported values
    "completed",
    "not-completed",
    "all"
  };


  // Allocate memory for the printer...
  if ((printer = calloc(1, sizeof(pappl_printer_t))) == NULL)
  {
    papplLog(system, PAPPL_LOGLEVEL_ERROR, "Unable to allocate memory for printer: %s", strerror(errno));
    return (NULL);
  }

  // Prepare URI values for the printer attributes...
  snprintf(resource, sizeof(resource), "/ipp/print/%s", printer_name);

  _papplSystemMakeUUID(system, printer_name, 0, uuid, sizeof(uuid));

  // Get the maximum spool size based on the size of the filesystem used for
  // the spool directory.  If the host OS doesn't support the statfs call
  // or the filesystem is larger than 2TiB, always report INT_MAX.
  if (statfs(system->directory, &spoolinfo))
    k_supported = INT_MAX;
  else if ((spoolsize = (double)spoolinfo.f_bsize * spoolinfo.f_blocks / 1024) > INT_MAX)
    k_supported = INT_MAX;
  else
    k_supported = (int)spoolsize;

  // Create the driver and assemble the final list of document formats...
//  printer->driver = lprintCreateDriver(driver_name);

  num_formats = 0;
  formats[num_formats ++] = "application/octet-stream";

  if (printer->driver_data.format && strcmp(printer->driver_data.format, "application/octet-stream"))
    formats[num_formats ++] = printer->driver_data.format;

#ifdef HAVE_LIBJPEG
  formats[num_formats ++] = "image/jpeg";
#endif // HAVE_LIBJPEG
#ifdef HAVE_LIBPNG
  formats[num_formats ++] = "image/png";
#endif // HAVE_LIBPNG
  formats[num_formats ++] = "image/pwg-raster";
  formats[num_formats ++] = "image/urf";

  // Initialize printer structure and attributes...
  pthread_rwlock_init(&printer->rwlock, NULL);

  printer->system         = system;
  printer->name           = strdup(printer_name);
  printer->dns_sd_name    = strdup(printer_name);
  printer->resource       = strdup(resource);
  printer->resourcelen    = strlen(resource);
  printer->device_uri     = strdup(device_uri);
  printer->driver_name    = strdup(driver_name);
  printer->attrs          = ippNew();
  printer->start_time     = time(NULL);
  printer->config_time    = printer->start_time;
  printer->state          = IPP_PSTATE_IDLE;
  printer->state_reasons  = PAPPL_PREASON_NONE;
  printer->state_time     = printer->start_time;
  printer->all_jobs       = cupsArrayNew3((cups_array_func_t)compare_all_jobs, NULL, NULL, 0, NULL, (cups_afree_func_t)_papplJobDelete);
  printer->active_jobs    = cupsArrayNew((cups_array_func_t)compare_active_jobs, NULL);
  printer->completed_jobs = cupsArrayNew((cups_array_func_t)compare_completed_jobs, NULL);
  printer->next_job_id    = 1;

  // charset-configured
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_CHARSET), "charset-configured", NULL, "utf-8");

  // charset-supported
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_CHARSET), "charset-supported", sizeof(charset) / sizeof(charset[0]), NULL, charset);

  // compression-supported
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "compression-supported", (int)(sizeof(compression) / sizeof(compression[0])), NULL, compression);

  // copies-default
  ippAddInteger(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "copies-default", 1);

  // copies-supported
  ippAddRange(printer->attrs, IPP_TAG_PRINTER, "copies-supported", 1, 999);

  // document-format-default
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_MIMETYPE), "document-format-default", NULL, "application/octet-stream");

  // document-format-supported
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_MIMETYPE, "document-format-supported", num_formats, NULL, formats);

  // generated-natural-language-supported
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_LANGUAGE), "generated-natural-language-supported", NULL, "en");

  // identify-actions-default
  ippAddString (printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "identify-actions-default", NULL, "sound");

  // identify-actions-supported
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "identify-actions-supported", sizeof(identify_actions) / sizeof(identify_actions[0]), NULL, identify_actions);

  // ipp-features-supported
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "ipp-features-supported", sizeof(ipp_features) / sizeof(ipp_features[0]), NULL, ipp_features);

  // ipp-versions-supported
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "ipp-versions-supported", (int)(sizeof(ipp_versions) / sizeof(ipp_versions[0])), NULL, ipp_versions);

  // job-creation-attributes-supported
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-creation-attributes-supported", (int)(sizeof(job_creation_attributes) / sizeof(job_creation_attributes[0])), NULL, job_creation_attributes);

  // job-ids-supported
  ippAddBoolean(printer->attrs, IPP_TAG_PRINTER, "job-ids-supported", 1);

  // job-k-octets-supported
  ippAddRange(printer->attrs, IPP_TAG_PRINTER, "job-k-octets-supported", 0, k_supported);

  // job-priority-default
  ippAddInteger(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "job-priority-default", 50);

  // job-priority-supported
  ippAddInteger(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "job-priority-supported", 1);

  // job-sheets-default
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_NAME), "job-sheets-default", NULL, "none");

  // job-sheets-supported
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_NAME), "job-sheets-supported", NULL, "none");

  // media-col-supported
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "media-col-supported", (int)(sizeof(media_col) / sizeof(media_col[0])), NULL, media_col);

  // multiple-document-handling-supported
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "multiple-document-handling-supported", sizeof(multiple_document_handling) / sizeof(multiple_document_handling[0]), NULL, multiple_document_handling);

  // multiple-document-jobs-supported
  ippAddBoolean(printer->attrs, IPP_TAG_PRINTER, "multiple-document-jobs-supported", 0);

  // multiple-operation-time-out
  ippAddInteger(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "multiple-operation-time-out", 60);

  // multiple-operation-time-out-action
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "multiple-operation-time-out-action", NULL, "abort-job");

  // natural-language-configured
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_LANGUAGE), "natural-language-configured", NULL, "en");

  // operations-supported
  ippAddIntegers(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "operations-supported", (int)(sizeof(operations) / sizeof(operations[0])), operations);

  // orientation-requested-default
  ippAddInteger(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "orientation-requested-default", IPP_ORIENT_NONE);

  // orientation-requested-supported
  ippAddIntegers(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "orientation-requested-supported", (int)(sizeof(orientation_requested) / sizeof(orientation_requested[0])), orientation_requested);

  // pdl-override-supported
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "pdl-override-supported", NULL, "attempted");

  // print-color-mode-default
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-color-mode-default", NULL, "monochrome");

  // print-color-mode-supported
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-color-mode-supported", (int)(sizeof(print_color_mode) / sizeof(print_color_mode[0])), NULL, print_color_mode);

  // print-content-optimize-default
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-content-optimize-default", NULL, "auto");

  // print-content-optimize-supported
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-content-optimize-supported", (int)(sizeof(print_content_optimize) / sizeof(print_content_optimize[0])), NULL, print_content_optimize);

  // print-quality-default
  ippAddInteger(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "print-quality-default", IPP_QUALITY_NORMAL);

  // print-quality-supported
  ippAddIntegers(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "print-quality-supported", (int)(sizeof(print_quality) / sizeof(print_quality[0])), print_quality);

  // printer-get-attributes-supported
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "printer-get-attributes-supported", NULL, "document-format");

  // printer-info
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-info", NULL, printer_name);

  // printer-kind
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "printer-kind", (int)(sizeof(printer_kind) / sizeof(printer_kind[0])), NULL, printer_kind);

  // printer-name
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_NAME, "printer-name", NULL, printer_name);

  // printer-settable-attributes
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "printer-settable-attributes", (int)(sizeof(printer_settable_attributes) / sizeof(printer_settable_attributes[0])), NULL, printer_settable_attributes);

  // printer-strings-languages-supported
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_LANGUAGE, "printer-strings-languages-supported", (int)(sizeof(printer_strings_languages) / sizeof(printer_strings_languages[0])), NULL, printer_strings_languages);

  // printer-uuid
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-uuid", NULL, uuid);

  // uri-authentication-supported
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "uri-authentication-supported", 2, NULL, uri_authentication);

  // uri-security-supported
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "uri-security-supported", 2, NULL, uri_security);

  // which-jobs-supported
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "which-jobs-supported", sizeof(which_jobs) / sizeof(which_jobs[0]), NULL, which_jobs);

  // Add the printer to the system...
  pthread_rwlock_wrlock(&system->rwlock);

  if (printer_id)
    printer->printer_id = printer_id;
  else
    printer->printer_id = system->next_printer_id ++;

  if (!system->printers)
    system->printers = cupsArrayNew3((cups_array_func_t)compare_printers, NULL, NULL, 0, NULL, (cups_afree_func_t)free_printer);

  cupsArrayAdd(system->printers, printer);

  if (!system->default_printer_id)
    system->default_printer_id = printer->printer_id;

  pthread_rwlock_unlock(&system->rwlock);

  // Register the printer with Bonjour...
  if (system->subtypes)
    _papplPrinterRegisterDNSSD(printer);

  // Return it!
  return (printer);
}


//
// 'papplPrinterDelete()' - Delete a printer.
//

void
papplPrinterDelete(pappl_printer_t *printer)	/* I - Printer */
{
  // Remove the printer from the system object...
  pthread_rwlock_wrlock(&printer->system->rwlock);
  cupsArrayRemove(printer->system->printers, printer);
  pthread_rwlock_unlock(&printer->system->rwlock);
}


//
// 'papplSystemFindPrinter()' - Find a printer by resource...
//

pappl_printer_t *			// O - Printer or `NULL` if none
papplSystemFindPrinter(
    pappl_system_t *system,		// I - System
    const char      *resource,		// I - Resource path or `NULL`
    int             printer_id)		// I - Printer ID or `0`
{
  pappl_printer_t	*printer;	// Matching printer


  papplLog(system, PAPPL_LOGLEVEL_DEBUG, "papplSystemFindPrinter(system, resource=\"%s\", printer_id=%d)", resource, printer_id);

  pthread_rwlock_rdlock(&system->rwlock);

  if (resource && (!strcmp(resource, "/ipp/print") || (!strncmp(resource, "/ipp/print/", 11) && isdigit(resource[11] & 255))))
  {
    printer_id = system->default_printer_id;
    resource   = NULL;

    papplLog(system, PAPPL_LOGLEVEL_DEBUG, "papplSystemFindPrinter: Looking for default printer_id=%d", printer_id);
  }

  for (printer = (pappl_printer_t *)cupsArrayFirst(system->printers); printer; printer = (pappl_printer_t *)cupsArrayNext(system->printers))
  {
    papplLog(system, PAPPL_LOGLEVEL_DEBUG, "papplSystemFindPrinter: printer '%s' - resource=\"%s\", printer_id=%d", printer->name, printer->resource, printer->printer_id);

    if (resource && !strncmp(printer->resource, resource, printer->resourcelen) && (!resource[printer->resourcelen] || resource[printer->resourcelen] == '/'))
      break;
    else if (printer->printer_id == printer_id)
      break;
  }
  pthread_rwlock_unlock(&system->rwlock);

  papplLog(system, PAPPL_LOGLEVEL_DEBUG, "papplSystemFindPrinter: Returning %p(%s)", printer, printer ? printer->name : "none");

  return (printer);
}


//
// '_papplGetRand()' - Return the best 32-bit random number we can.
//

unsigned				// O - Random number
_papplGetRand(void)
{
#ifdef HAVE_ARC4RANDOM
  // arc4random uses real entropy automatically...
  return (arc4random());

#else
#  ifdef HAVE_GETRANDOM
  // Linux has the getrandom function to get real entropy, but can fail...
  unsigned	buffer;			// Random number buffer

  if (getrandom(&buffer, sizeof(buffer), 0) == sizeof(buffer))
    return (buffer);

#  elif defined(HAVE_GNUTLS_RND)
  // GNU TLS has the gnutls_rnd function we can use as well, but can fail...
  unsigned	buffer;			// Random number buffer

  if (!gnutls_rnd(GNUTLS_RND_NONCE, &buffer, sizeof(buffer)))
    return (buffer);
#  endif // HAVE_GETRANDOM

  // Fall back to random() seeded with the current time - not ideal, but for
  // our non-cryptographic purposes this is OK...
  static int first_time = 1;		// First time we ran?

  if (first_time)
  {
    srandom(time(NULL));
    first_time = 0;
  }

  return ((unsigned)random());
#endif // __APPLE__
}


//
// 'compare_active_jobs()' - Compare two active jobs.
//

static int				// O - Result of comparison
compare_active_jobs(pappl_job_t *a,	// I - First job
                    pappl_job_t *b)	// I - Second job
{
  return (b->job_id - a->job_id);
}


//
// 'compare_jobs()' - Compare two jobs.
//

static int				// O - Result of comparison
compare_all_jobs(pappl_job_t *a,	// I - First job
                 pappl_job_t *b)	// I - Second job
{
  return (b->job_id - a->job_id);
}


//
// 'compare_completed_jobs()' - Compare two completed jobs.
//

static int				// O - Result of comparison
compare_completed_jobs(pappl_job_t *a,	// I - First job
                       pappl_job_t *b)	// I - Second job
{
  return (b->job_id - a->job_id);
}


//
// 'compare_printers()' - Compare two printers.
//

static int				// O - Result of comparison
compare_printers(pappl_printer_t *a,	// I - First printer
                 pappl_printer_t *b)	// I - Second printer
{
  return (strcmp(a->name, b->name));
}


//
// 'free_printer()' - Free the memory used by a printer.
//

static void
free_printer(pappl_printer_t *printer)	// I - Printer
{
  // Remove DNS-SD registrations...
  _papplPrinterUnregisterDNSSD(printer);

  // Free memory...
  free(printer->name);
  free(printer->dns_sd_name);
  free(printer->location);
  free(printer->geo_location);
  free(printer->organization);
  free(printer->org_unit);
  free(printer->resource);
  free(printer->device_uri);
  free(printer->driver_name);

//  lprintDeleteDriver(printer->driver);
  ippDelete(printer->driver_attrs);
  ippDelete(printer->attrs);

  cupsArrayDelete(printer->active_jobs);
  cupsArrayDelete(printer->completed_jobs);
  cupsArrayDelete(printer->all_jobs);

  free(printer);
}
