<!--
vl-release staged release notes: the user-facing story of the NEXT release.
Maintained continuously while working; `vlr prepare` promotes it into the release notes
history (and the GitHub release) and resets this file to this template.

Format: the first line is "# <release title>" WITHOUT a version number (vlr adds it).
Everything after the title is the Markdown release body. Describe the resulting behavior
for users and operators; keep it representative of what actually ships.
-->
# Long generated passwords are accepted again

Changing a password (including the first-login change of the `admin` password) refused most long, randomly
generated passwords with "New password does not meet password policy". The dictionary check rejected any
password that contained a common word of three or more letters anywhere inside it, so a 64-character password
from a password manager almost always failed on some fragment like `doc` or `bet`, and the longer the password,
the more likely it was to be refused.

The dictionary check now refuses a password only when the password itself is a dictionary word, ignoring case
and any digits or symbols around it (`Sunshine2024!` is still refused). Registration and password changes now
apply the same policy:

- 8 to 128 characters.
- Passwords shorter than 20 characters need at least one letter and one digit. Longer passwords and
  passphrases (`correct-horse-battery-staple`) don't.
- Not a dictionary word, not on the common-password lists, and not found in public breaches.

When a password is refused, the error now says which rule it broke.
