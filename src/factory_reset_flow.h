#pragma once

namespace chimera {
enum class ResetBoot { Normal, Complete, Restart, Failed };
// Store reads: -1 absent, -2 invalid/unreadable, otherwise persisted attempt count.
// No communication stacks may be running while this flow executes.
template<class Store> ResetBoot runFactoryReset(Store& s) {
  const int intent = s.intent();
  const int journal = s.journal();
  // An explicit new request (0) overrides a failed/old journal.
  const bool fresh = intent == 0;
  if (!fresh && journal == -2) return ResetBoot::Failed;
  if (!fresh && journal == -1 && intent == -2) return ResetBoot::Failed;
  if (!fresh && journal == -1 && intent == -1) {
    const int done = s.completed(); // Same -1 absent / -2 error / attempt encoding.
    return done == -2 ? ResetBoot::Failed : done == -1 ? ResetBoot::Normal : ResetBoot::Complete;
  }
  const bool resume = !fresh && journal >= 0;
  const int attempts = resume ? journal : intent;
  if (attempts < 0 || attempts >= 3) return ResetBoot::Failed;
  const int next = attempts + 1;
  if (!resume) {
    // Persist BEFORE erasing, so power loss cannot cause unlimited attempts.
    if (!s.saveIntent(next) || !s.formatSettings()) return ResetBoot::Failed;
  }
  // Survives partial/full NVS erase. Failure never triggers an automatic reboot.
  if (!s.saveJournal(next) || !s.eraseNvs() || !s.finish()) return ResetBoot::Failed;
  return ResetBoot::Restart;
}
}
