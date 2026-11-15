-- Update が false を返したら、Draw を呼ぶ前にこの関数から戻してください。
-- この関数だけを編集し、Editor の Save and Play で結果を確かめます。
local function run_frame(update, draw)
    update()
    draw()
end

-- ここから下は、成功時と失敗時の呼び出し順を調べる。
local function trace_frame(updateSucceeded)
    local trace = ""
    run_frame(function()
        trace = trace .. "Update"
        return updateSucceeded
    end, function()
        trace = trace .. " → Draw"
    end)
    return trace
end

return {
    start = function(self)
        local success = trace_frame(true)
        local failure = trace_frame(false)
        local passed = success == "Update → Draw" and failure == "Update"
        self:reference("result"):set_text(
            "更新成功  " .. success .. "\n更新失敗  " .. failure .. "\n\n" ..
            (passed and "両方正解。更新に失敗したときだけ描画を飛ばせました。"
                or "Update の戻り値を使って、失敗した場合だけ早く戻りましょう。"))
    end,
}
