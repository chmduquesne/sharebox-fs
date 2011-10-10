Here is sharebox, a project of a distributed synchronizing filesystem.

The problem
===========

We want a filesystem that will synchronize arbitrary data between several
machines. This filesystem has to be simple, and to provide as efficiently
as possible a set of minimum features:

- *Offline access*: it should be possible to modify data while not being
  connected to the internet, and get the data synchronized when the
  connection is restored.
- *Conflict handling*: if the same document is modified in several places,
  the system should keep both versions and yet provide a simple way to
  choose between the two versions. Ideally the remote conflicting version
  should be shown as a hidden file. Removing this file should resolve the
  conflict in favor of the local version. Moving this file as the new
  version should resolve the conflict in favor of the remote version.
  True merges should be avoided as they could be both confusing and
  difficult to handle. Solved conflicts should propagate: if the conflict
  between A and B is solved on A, B should follow the same choice.
- *Versioning*: It should be able to automatically keep several versions
  of document.
- *Efficient storage*: Versioning must not induce a forever growing size.
  It should provide tools to automatically clean the filesystem.
- *Useability*: Users should not have to be aware of the internals.
  Preferably, we should avoid creating special commands. Browsing the
  history should be done through regular 'ls' and 'cd' commands. Cleaning
  old versions of deleting snapshot should be doable through 'rm'. Moving
  content between machines should be doable through 'mv'.
- *Battery friendliness*: The filesystem should not force the user to run
  updates every time a file is modified. Instead, it should let the user
  schedule the synchronizations. Still, callbacks have to be provided in
  for those who wish to have these instant updates.

Planned interface
=================

Here is how the user should see the filesystem:

    mnt/
       |-regular files
       |-.sharebox/
                  |-history/
                  |-peers/

The test suite gives a sequence of use cases that may be interesting to
browse if you want to understand how sharebox should be used in the end.

Internal storage
================

Storage should be done through git-annex. Default for each file should be
to be versioned through git-annex. Transfers should happen only when files
are actually opened (through the open() syscall). In order to allow the
user to use git inside the filesystem, the .git directory of sharebox has
to be removed from the tree. This is achieved easily by moving the files
in a subtree:

    fs/
      |-.git/
      |-files/-regular files (links handled through git-annex)
