return {
    message = function(self, _, name)
        if name == "shortcut" then
            self:set_text("強い風に乗れば、向こうの風車へひと跳び")
        elseif name == "updraft" then
            self:set_text("上昇気流で浮かびながら、次の島を狙おう")
        elseif name == "crosswind" then
            self:set_text("横風に逆らって、着地点を狙おう")
        elseif name == "relay" then
            self:set_text("風見の浮島　　風車の風を乗り継いで雲の上へ")
        end
    end
}
