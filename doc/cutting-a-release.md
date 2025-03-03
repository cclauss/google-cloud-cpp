# Creating a new release of google-cloud-cpp

Unless there are no changes, we create releases for `google-cloud-cpp` every
4 weeks, or if there is a major announcement or change to the status of one
of the libraries (like reaching the "Alpha" or "Beta" milestone).

The intended audience of this document are developers in the `google-cloud-cpp`
project that need to create a new release. The audience is expected to be
familiar with the project itself, [git][git-docs], [GitHub][github-guides],
[semantic versioning](https://semver.org).

## Preparing for a release

First you should collect and update the release notes for the project. Prepare
a pull request (PR) with the necessary changes to the README files in each
project.

Assuming you are working on your own fork of the `google-cloud-cpp` project,
and `upstream` points to the `googleapis/google-cloud-cpp` remote,
these commands should be useful in identifying important changes:

```bash
# Summarize the output of this into google/cloud/bigtable/README.md
git log upstream/master -- google/cloud/bigtable
```

```bash
# Summarize the output of this into google/cloud/storage/README.md
git log upstream/master -- google/cloud/storage
```

Do not forget to update `google/cloud/README.md` too:

```bash
git log upstream/master -- google/cloud ":(exclude)google/cloud/storage" ":(exclude)google/cloud/bigtable"
```

It is not recommended that you create the release branch before this PR is
ready, but in some circumstances it might be needed, for example, if a large
change that could destabilize the release is about to be merged, or if we want
to create the release at an specific point in the revision history.

## Create the release branch

To find the next release number, look at the existing branch names on the
`upstream` repo and select the next available "v.0.N" version number.

```bash
git remote show upstream
```

Throughout this document we will use this variable to represent the release
name:

```bash
# Use the actual release prefix (e.g. v0.5) not just `N`.
export RELEASE="v0.N"
```

If you decide to cut&paste the commands below, make sure that variable has the
actual release value, e.g. `v0.5` or `v0.7`, and not the generic `N`.

Clone the main repository to create the branch:

```bash
git clone git@github.com:googleapis/google-cloud-cpp.git releases
cd releases
git checkout -b "${RELEASE}.x"
```

Modify the CMake script to indicate this is a release:

```bash
sed -i 's/set(GOOGLE_CLOUD_CPP_IS_RELEASE "")/set(GOOGLE_CLOUD_CPP_IS_RELEASE "yes")/' google/cloud/CMakeLists.txt
```

Run the CMake configuration step to update the Bazel configuration files:

```bash
cmake -H. -Bcmake-out
```

You should expect that only `google/cloud/CMakeLists.txt` and
`google/cloud/*/internal/version_info.h` are modified. Verify this,
and then commit the changes and push the new branch:

```bash
git commit -m"Create ${RELEASE}.x release branch" .
git push --set-upstream origin "${RELEASE}.x"
```

**NOTE:** No code review or Pull Request is needed as part of this step.

## Generate and upload the documentation to googleapis.dev

Manually run a Kokoro job
`cloud-devrel/client-libraries/cpp/google-cloud-cpp/refdocs` in the Cloud C++
internal testing dashboard and specify the branch name (e.g. `v0.11.x`) in the
`Committish` field. This job will generate and upload the doxygen documentation
to the staging bucket for googleapis.dev hosting. The uploaded documentation
will generally be live in an hour at URLs like
`https://googleapis.dev/cpp/google-cloud-bigtable/latest/`.

You are now finished with this "releases" clone of the repo that we created in
the instructions above. You may now remove this directory.

## Bump the version numbers in `master`

Working in your fork of `gooogle-cloud-cpp`: bump the version numbers to the
*next* version (i.e., one version past the release you just did above), and
send the PR for review against `master`. For an example, look at
[#1962](https://github.com/googleapis/google-cloud-cpp/pull/1962)

## Create a pre-release tag

Create a pre-release using
[GitHub](https://github.com/googleapis/google-cloud-cpp/releases/new).
Make sure you reference the `v0.N.x` branch, set a tag name like `v0.N.0-pre1`,
and check the `pre-release` checkbox.

Copy the relevant release notes into the description of the release.

After you create the release, capture the SHA256 checksums of the
tarball and zip files, and edit the notes to include them. These
commands might be handy:

```bash
TAG="v0.6.0-pre1" # change this to the actual pre-release tag
wget -q -O - "https://github.com/googleapis/google-cloud-cpp/archive/${TAG}.tar.gz" | sha256sum
wget -q -O - "https://github.com/googleapis/google-cloud-cpp/archive/${TAG}.zip" | sha256sum
```

## Have the pre-release tag reviewed

Talk to your colleagues, make sure the pre-release tag looks Okay. There are
(at the moment), no tests beyond the CI build for the branch.

If there are any changes to the code in the branch you need to create a new
pre-release, and iterate until you are satisfied with the code.

## Promote the pre-release tag to an actual release

Edit the pre-release, change the name, change the tag, uncheck the pre-release
checkbox and publish.

After you publish, remember to update the SHA256 sums, they change, as
the tarball and zip files include the tag as part of the file paths.

```bash
TAG="v0.6.0" # change this to the actual release tag
wget -q -O - "https://github.com/googleapis/google-cloud-cpp/archive/${TAG}.tar.gz" | sha256sum
wget -q -O - "https://github.com/googleapis/google-cloud-cpp/archive/${TAG}.zip" | sha256sum
```

## Review the branch protections

We use the [GitHub Branch Settings][github-branch-settings] to protect the
release branches against accidental mistakes. From time to time changes in the
release branch naming conventions may require you to change these settings.
Please note that we use more strict settings for release branches than for
`master`, in particular:

* We require at least one review, but stale reviews are dismissed.
* The `Require status checks to pass before merging` option is set.
  This prevents merges into the release branches that break the build.
  * The `Require branches to be up to date before merging` sub-option
    is set. This prevents two merges that do not conflict, but nevertheless
    break if both are pushed, to actually merge.
  * The `Kokoro Ubuntu`, `Kokoro Windows`, `cla/google`, and
    `continuous-integration/travis-ci` checks are required to pass.

* The `Include administrators` checkbox is turned on, we want to stop ourselves
  from making mistakes.

* Turn on the `Restrict who can push to matching branches`. Only Google team
  members should be pushing to release branches.

[git-docs]: https://git-scm.com/doc
[github-guides]: https://guides.github.com/
[github-branch-settings]: https://github.com/googleapis/google-cloud-cpp/settings/branches

## Push the release to Microsoft vcpkg

Nudge coryan@ to send a PR to
[vcpkg](https://github.com/Microsoft/vcpkg/tree/master/ports/google-cloud-cpp).
