-- Mechanar: Jagged Blue Crystal (30436) / Jagged Red Crystal (30437) were missing their
-- "Use:" spell 36565 (Combine Crystals): consumes both crystals and creates 30438
-- (Cache of the Legion Key). Values from official DBC (itemeffect: spell 36565, trigger USE).
UPDATE `item_template` SET `spellid_1` = 36565, `spelltrigger_1` = 0, `spellcharges_1` = 0, `spellcooldown_1` = -1, `spellcategory_1` = 0, `spellcategorycooldown_1` = -1 WHERE `entry` IN (30436, 30437);
