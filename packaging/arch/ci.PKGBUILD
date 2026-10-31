# Packages a tree CI has already built (.github/workflows/build.yml), so the release package
# is not built twice: makepkg -p ci.PKGBUILD with PHI_BUILD_DIR set to that build folder.
# Names, dependencies and licences come from the PKGBUILD next to it.
source ./PKGBUILD
unset -f pkgver build

pkgname=phi
pkgver=${PHI_PKGVER:-$(sed -n 's/^project(Phi VERSION \([0-9.]*\).*/\1/p' "$startdir/../../CMakeLists.txt")}
conflicts=('phi-git' 'phinyx-git')
replaces=()
source=()
sha256sums=()
# The stock plugins come stripped from their own builds; stripping again only costs time
options=('!strip' '!debug')

package() {
  DESTDIR="$pkgdir" cmake --install "$PHI_BUILD_DIR"
  # JUCE installs its headers, CMake files and juceaide build helper too; PHI needs none of them
  rm -rf "$pkgdir"/usr/{bin,include,lib/cmake}/JUCE-*
}
