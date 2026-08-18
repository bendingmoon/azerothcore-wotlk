--
-- `.character setreputation` command permission (custom permission 1005, see RBAC.h "custom permissions 1000+")
INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(1005, 'Command: character setreputation');

-- Gamemaster Commands role (Administrator role inherits it through Gamemaster role)
INSERT INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES
(197, 1005);
