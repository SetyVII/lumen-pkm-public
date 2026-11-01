// Compatibility entrypoint for the original native integration command.
// The acceptance suite targets the contextual editor, asynchronous local saves,
// and shared native views. Media parsing and keyboard controls have their own
// editor-media suite; backend durability/assets are exercised by Go API tests.
// Both native entrypoints require an explicitly disposable loopback API.
#include "acceptance.cpp"
