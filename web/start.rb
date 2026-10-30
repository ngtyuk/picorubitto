Funicular.env = 'production'
page = JS.global[:GamingWeb][:page].to_s
component = case page
            when 'studio' then StudioComponent
            when 'runner' then RunnerComponent
            else EmulatorComponent
            end
Funicular.start(component, container: 'app')
