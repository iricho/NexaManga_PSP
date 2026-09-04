#pragma once

struct SDL_Renderer;

namespace BootDiagnostics {

void initialize(const char* executablePath);
void stage(int number, const char* label);
void checkpoint(int number, const char* label);
void checkpointLogOnly(const char* id, const char* label);
void note(const char* key, const char* value);
void noteUnsigned(const char* key, unsigned long value);
void holdIfRequested(int number, SDL_Renderer* renderer = nullptr);
void holdIfCheckpointRequested(int number);
void fatal(const char* message);
int lastStage();
const char* logPath();

} // namespace BootDiagnostics
