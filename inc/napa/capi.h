// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#pragma once

#include "napa/exports.h"
#include "napa/types.h"

#include <v8.h>

/// <summary> Creates a napa zone. </summary>
/// <param name="id"> A unique id for the zone. </param>
/// <remarks>
///     This function returns a handle that must be release when it's no longer needed.
///     Napa keeps track of all zone handles and destroys the zone when all handles have been released.
/// </remarks>
EXTERN_C NAPA_API napa_zone_handle napa_zone_create(napa_string_ref id);

/// <summary> Retrieves a zone by id. </summary>
/// <param name="id"> A unique id for the zone. </param>
/// <returns> The zone handle if exists, null otherwise. </returns>
/// <remarks>
///     This function returns a handle that must be release when it's no longer needed.
///     Napa keeps track of all zone handles and destroys the zone when all handles have been released.
/// </remarks>
EXTERN_C NAPA_API napa_zone_handle napa_zone_get(napa_string_ref id);

/// <summary> Retrieves the current zone. </summary>
/// <returns> The zone handle if this thread is associated with one, null otherwise. </returns>
/// <remarks>
///     This function returns a handle that must be release when it's no longer needed.
///     Napa keeps track of all zone handles and destroys the zone when all handles have been released.
/// </remarks>
EXTERN_C NAPA_API napa_zone_handle napa_zone_get_current();

/// <summary> Releases the zone handle. When all handles for a zone are released the zone is recycled. </summary>
/// <param name="handle"> The zone handle. </param>
/// <remarks>
///     Depending on the Zone's recycle mode (specified when initialized), release behavior may varies. See
///     also remarks of napa_zone_recycle() for more information.
/// </remarks>
EXTERN_C NAPA_API napa_result_code napa_zone_release(napa_zone_handle handle);

/// <summary>
///     Initializes the napa zone, providing specific settings.
///     The provided settings override any settings that were previously set.
///     TODO: specify public settings here
/// </summary>
/// <param name="handle"> The zone handle. </param>
/// <param name="settings"> The settings string. </param>
EXTERN_C NAPA_API napa_result_code napa_zone_init(
    napa_zone_handle handle,
    napa_string_ref settings);

/// <summary> Retrieves the zone id. </summary>
/// <param name="handle"> The zone handle. </param>
EXTERN_C NAPA_API napa_string_ref napa_zone_get_id(napa_zone_handle handle);

/// <summary> Executes a pre-loaded function asynchronously on all zone workers. </summary>
/// <param name="handle"> The zone handle. </param>
/// <param name="spec"> The function spec to call. </param>
/// <param name="callback"> A callback that is triggered when broadcast is done. </param>
/// <param name="context"> An opaque pointer that is passed back in the callback. </param>
EXTERN_C NAPA_API void napa_zone_broadcast(
    napa_zone_handle handle,
    napa_zone_function_spec spec,
    napa_zone_broadcast_callback callback,
    void* context);

/// <summary> Executes a pre-loaded function asynchronously in a single zone worker. </summary>
/// <param name="handle"> The zone handle. </param>
/// <param name="spec"> The function spec to call. </param>
/// <param name="callback"> A callback that is triggered when execution is done. </param>
/// <param name="context"> An opaque pointer that is passed back in the callback. </param>
EXTERN_C NAPA_API void napa_zone_execute(
    napa_zone_handle handle,
    napa_zone_function_spec spec,
    napa_zone_execute_callback callback,
    void* context);

/// <summary>
///     Signal the zone to get ready for recycling. 
/// </summary>
/// <param name="handle"> The zone handle. </param>
/// <remarks>
///     After calling this function, the zone will no longer process future requests from
///     broadcast() or execute() calls. The current processing and queued tasks will be processed.
///     After all tasks completed, the zone will exit.
///
///     Zones are created with option "--recycle auto" by default. This means when a zone is being
///     released, a call of napa_zone_recycle() to the zone will be performed automatically.
///     A user can create a persistent zone with option "--recycle manual". It will not be recycled until
///     user calls napa_zone_recycle() manually.
/// </remarks>
EXTERN_C NAPA_API void napa_zone_recycle(napa_zone_handle handle);


// <summary>
EXTERN_C NAPA_API void napa_zone_on(napa_zone_handle handle, const char* eventName, v8::Local<v8::Function> jsFunc);


/// <summary>
///     Global napa initialization. Invokes initialization steps that are cross zones.
///     The settings passed represent the defaults for all the zones
///     but can be overriden in the zone initialization API.
///     TODO: specify public settings here.
/// </summary>
/// <param name="settings"> The settings string. </param>
EXTERN_C NAPA_API napa_result_code napa_initialize(napa_string_ref settings);

/// <summary>
///     Same as napa_initialize only accepts arguments as provided by console
/// </summary>
/// <param name="argc"> Number of arguments. </param>
/// <param name="argv"> The arguments. </param>
EXTERN_C NAPA_API napa_result_code napa_initialize_from_console(
    int argc,
    const char* argv[]);

/// <summary> Invokes napa shutdown steps. All non released zones will be destroyed. </summary>
EXTERN_C NAPA_API napa_result_code napa_shutdown();

/// <summary> Convert the napa result code to its string representation. </summary>
/// <param name="code"> The result code. </param>
EXTERN_C NAPA_API const char* napa_result_code_to_string(napa_result_code code);

/// <summary> Set customized allocator, which will be used for napa_allocate and napa_deallocate.
/// If user doesn't call napa_allocator_set, C runtime malloc/free from napa.dll will be used. </summary>
/// <param name="allocate_callback"> Function pointer for allocating memory, which should be valid during the entire process. </param>
/// <param name="deallocate_callback"> Function pointer for deallocating memory, which should be valid during the entire process. </param>
EXTERN_C NAPA_API void napa_allocator_set(
    napa_allocate_callback allocate_callback, 
    napa_deallocate_callback deallocate_callback);

/// <summary> Allocate memory using napa allocator from napa_allocator_set, which is using C runtime ::malloc if not called. </summary>
/// <param name="size"> Size of memory requested in byte. </param>
/// <returns> Allocated memory. </returns>
EXTERN_C NAPA_API void* napa_allocate(size_t size);

/// <summary> Free memory using napa allocator from napa_allocator_set, which is using C runtime ::free if not called. </summary>
/// <param name="pointer"> Pointer to memory to be freed. </param>
/// <param name="size_hint"> Hint of size to deallocate. </param>
EXTERN_C NAPA_API void napa_deallocate(void* pointer, size_t size_hint);

/// <summary> Allocate memory using C runtime ::malloc from napa.dll. </summary>
/// <param name="size"> Size of memory requested in byte. </param>
/// <returns> Allocated memory. </returns>
EXTERN_C NAPA_API void* napa_malloc(size_t size);

/// <summary> Free memory using C runtime ::free from napa.dll. </summary>
/// <param name="pointer"> Pointer to memory to be freed. </param>
/// <param name="size_hint"> Hint of size to deallocate. </param>
EXTERN_C NAPA_API void napa_free(void* pointer, size_t size_hint);
