#pragma once

/** Ensure VESPAIcons.qrc is registered (shared plugins skip ctor Q_INIT_RESOURCE). */
void vespa_qrc_init();
